//===-- optimization/Optimizer.cpp ------------------------------*- C++ -*-===//
#include "Optimizer.hpp"
#include <GL/glew.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <iostream>
#include <fstream>
#include <sstream>

namespace warehouse {


Optimizer::Optimizer(const WarehouseData& data, float cellSize)
    : m_data(data), m_cellSize(cellSize) {}

bool Optimizer::isInsidePolygon(float x, float y) const {
    const auto& p = m_data.perimeter;
    bool inside = false;
    const std::size_t n = p.size();
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const bool yi = (p[i].y > y);
        const bool yj = (p[j].y > y);
        if (yi != yj) {
            const float xCross =
                (p[j].x - p[i].x) * (y - p[i].y) / (p[j].y - p[i].y) + p[i].x;
            if (x < xCross) inside = !inside;
        }
    }
    return inside;
}

void Optimizer::initBaseGrid() {
    const float W = m_data.maxX - m_data.minX;
    const float H = m_data.maxY - m_data.minY;
    m_gridW = std::max(1, static_cast<int>(std::ceil(W / m_cellSize)));
    m_gridH = std::max(1, static_cast<int>(std::ceil(H / m_cellSize)));
    
    // Using packed 32-bit uints for 4 cells per word
    int totalWords = (m_gridW * m_gridH + 3) / 4;
    m_baseGridPacked.assign(totalWords, 0);

    auto setCell = [&](int gx, int gy, uint8_t val) {
        int idx = gy * m_gridW + gx;
        int wordIdx = idx / 4;
        int byteOffset = (idx % 4) * 8;
        m_baseGridPacked[wordIdx] &= ~(0xFFu << byteOffset);
        m_baseGridPacked[wordIdx] |= (static_cast<uint32_t>(val) << byteOffset);
    };

    auto getCell = [&](int gx, int gy) -> uint8_t {
        int idx = gy * m_gridW + gx;
        int wordIdx = idx / 4;
        int byteOffset = (idx % 4) * 8;
        return (m_baseGridPacked[wordIdx] >> byteOffset) & 0xFFu;
    };

    long freeCells = 0;
    for (int gy = 0; gy < m_gridH; ++gy) {
        for (int gx = 0; gx < m_gridW; ++gx) {
            const float cx = gridToWorldX(gx) + 0.5f * m_cellSize;
            const float cy = gridToWorldY(gy) + 0.5f * m_cellSize;
            if (!isInsidePolygon(cx, cy)) {
                setCell(gx, gy, 1); // kSolid
            } else {
                ++freeCells;
            }
        }
    }

    for (const auto& obs : m_data.obstacles) {
        const int gx0 = std::max(0, worldToGridX(obs.x));
        const int gy0 = std::max(0, worldToGridY(obs.y));
        const int gx1 = std::min(m_gridW, worldToGridX(obs.x + obs.width) + 1);
        const int gy1 = std::min(m_gridH, worldToGridY(obs.y + obs.depth) + 1);
        for (int gy = gy0; gy < gy1; ++gy) {
            for (int gx = gx0; gx < gx1; ++gx) {
                if (getCell(gx, gy) == 0) {
                    setCell(gx, gy, 1);
                    --freeCells;
                }
            }
        }
    }

    m_freeStartArea = static_cast<double>(freeCells) * m_cellSize * m_cellSize;
}

unsigned int Optimizer::compileComputeShader() const {
#ifndef COMPUTE_SHADER_PATH
#define COMPUTE_SHADER_PATH "src/optimization/Optimizer.comp"
#endif

    std::ifstream shaderFile(COMPUTE_SHADER_PATH);
    if (!shaderFile.is_open()) {
        std::cerr << "COMPUTE SHADER FILE NOT FOUND: " << COMPUTE_SHADER_PATH << std::endl;
        throw std::runtime_error("Failed to open compute shader file");
    }
    std::stringstream buffer;
    buffer << shaderFile.rdbuf();
    std::string shaderCodeStr = buffer.str();
    const char* shaderCode = shaderCodeStr.c_str();

    unsigned int shader = glCreateShader(GL_COMPUTE_SHADER);
    glShaderSource(shader, 1, &shaderCode, nullptr);
    glCompileShader(shader);

    int success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, nullptr, infoLog);
        std::cerr << "COMPUTE SHADER COMPILATION FAILED:\n" << infoLog << std::endl;
        throw std::runtime_error("Failed to compile compute shader");
    }

    unsigned int program = glCreateProgram();
    glAttachShader(program, shader);
    glLinkProgram(program);

    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(program, 512, nullptr, infoLog);
        std::cerr << "COMPUTE PROGRAM LINKING FAILED:\n" << infoLog << std::endl;
        throw std::runtime_error("Failed to link compute shader program");
    }
    
    glDeleteShader(shader);
    return program;
}

struct ParamsBuffer {
    float minX;
    float minY;
    float cellSize;
    int gridW;
    int gridH;
    int numBayTypes;
    int numCeilingPoints;
    int maxPlacementsPerStrategy;
    double freeStartArea;
};

// Represents the struct inside GLSL with strict alignment rules
struct GpuBayType {
    int id;
    float width;
    float depth;
    float height;
    float gap;
    int nLoads;
    float price;
    float _pad;
};

struct GpuPlacedBay {
    int id;
    float x;
    float y;
    float rotation;
    float footprintWidth;
    float footprintDepth;
    float bayWidth;
    float bayDepth;
    float height;
    float gap;
    float _pad0;
    float _pad1;
};

struct GpuStrategyResult {
    double totalPrice;
    double totalLoads;
    double usedArea;
    double qScore;
    int placementCount;
    int _pad0;
    int _pad1;
    int _pad2;
};

struct GpuCeilingPoint {
    float x;
    float height;
};

struct GpuTrig {
    float c;
    float s;
};

Optimizer::Result Optimizer::run() {
    initBaseGrid();

    static constexpr int kNumStrategies = 24;
    RankMode rankModes[9] = {
        RankMode::LOADS_AREA_PRICE, RankMode::LOADS_PRICE,
        RankMode::AREA_PRICE, RankMode::LOADS2_PRICE,
        RankMode::CHEAPEST, RankMode::BIGGEST,
        RankMode::LOADS_ONLY, RankMode::SMALLEST,
        RankMode::PRICE_ONLY
    };

    // Calculate ranked indices for each rank mode
    std::vector<int> rankOrders[9];
    for (int r = 0; r < 9; ++r) {
        struct Ranked { int idx; double score; };
        std::vector<Ranked> ranked(m_data.bayTypes.size());
        for (size_t i = 0; i < m_data.bayTypes.size(); ++i) {
            const auto& t = m_data.bayTypes[i];
            const double area = static_cast<double>(t.width) * t.depth;
            double score = 0.0;
            switch (rankModes[r]) {
                case RankMode::LOADS_AREA_PRICE: score = (t.nLoads * area) / t.price; break;
                case RankMode::LOADS_PRICE: score = t.nLoads / t.price; break;
                case RankMode::AREA_PRICE: score = area / t.price; break;
                case RankMode::LOADS2_PRICE: score = (t.nLoads * t.nLoads) / t.price; break;
                case RankMode::CHEAPEST: score = 1.0 / t.price; break;
                case RankMode::BIGGEST: score = area; break;
                case RankMode::LOADS_ONLY: score = t.nLoads; break;
                case RankMode::SMALLEST: score = 1.0 / area; break;
                case RankMode::PRICE_ONLY: score = t.price; break;
            }
            ranked[i] = {static_cast<int>(i), score};
        }
        std::sort(ranked.begin(), ranked.end(), [](const Ranked& a, const Ranked& b) {
            if (a.score == b.score) return a.idx < b.idx;
            return a.score > b.score;
        });
        for (const auto& rItem : ranked) rankOrders[r].push_back(rItem.idx);
    }

    std::vector<int> allRankedIndices(kNumStrategies * m_data.bayTypes.size());
    for (int i = 0; i < kNumStrategies; ++i) {
        int r = (i / 16) % 9; // 16 variations per rank mode (8 dirs * 2 angles)
        for (size_t j = 0; j < m_data.bayTypes.size(); ++j) {
            allRankedIndices[i * m_data.bayTypes.size() + j] = rankOrders[r][j];
        }
    }

    std::vector<GpuBayType> gpuBayTypes(m_data.bayTypes.size());
    for (size_t i = 0; i < m_data.bayTypes.size(); ++i) {
        gpuBayTypes[i].id = m_data.bayTypes[i].id;
        gpuBayTypes[i].width = m_data.bayTypes[i].width;
        gpuBayTypes[i].depth = m_data.bayTypes[i].depth;
        gpuBayTypes[i].height = m_data.bayTypes[i].height;
        gpuBayTypes[i].gap = m_data.bayTypes[i].gap;
        gpuBayTypes[i].nLoads = m_data.bayTypes[i].nLoads;
        gpuBayTypes[i].price = m_data.bayTypes[i].price;
    }

    std::vector<GpuCeilingPoint> gpuCeiling(m_data.ceiling.size());
    for (size_t i = 0; i < m_data.ceiling.size(); ++i) {
        gpuCeiling[i].x = m_data.ceiling[i].x;
        gpuCeiling[i].height = m_data.ceiling[i].height;
    }

    std::vector<GpuTrig> gpuTrig(360);
    for (int i = 0; i < 360; ++i) {
        double rad = static_cast<double>(i) * 3.14159265358979323846 / 180.0;
        gpuTrig[i].c = static_cast<float>(std::cos(rad));
        gpuTrig[i].s = static_cast<float>(std::sin(rad));
    }

    unsigned int computeProgram = compileComputeShader();

    // Max placements is conservatively derived from warehouse area / smallest bay area
    // Setting a safe maximum to avoid excessive VRAM usage.
    const int maxPlacements = 100000;

    ParamsBuffer pBuf;
    pBuf.minX = m_data.minX;
    pBuf.minY = m_data.minY;
    pBuf.cellSize = m_cellSize;
    pBuf.gridW = m_gridW;
    pBuf.gridH = m_gridH;
    pBuf.numBayTypes = m_data.bayTypes.size();
    pBuf.numCeilingPoints = gpuCeiling.size();
    pBuf.maxPlacementsPerStrategy = maxPlacements;
    pBuf.freeStartArea = m_freeStartArea;

    unsigned int ssbo[10];
    glGenBuffers(10, ssbo);

    auto uploadSSBO = [&](int binding, unsigned int id, size_t size, const void* data) {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, id);
        glBufferData(GL_SHADER_STORAGE_BUFFER, size, data, GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, id);
    };

    uploadSSBO(0, ssbo[0], sizeof(ParamsBuffer), &pBuf);
    uploadSSBO(1, ssbo[1], gpuBayTypes.size() * sizeof(GpuBayType), gpuBayTypes.data());
    uploadSSBO(2, ssbo[2], allRankedIndices.size() * sizeof(int), allRankedIndices.data());
    uploadSSBO(3, ssbo[3], gpuCeiling.size() * sizeof(GpuCeilingPoint), gpuCeiling.data());
    uploadSSBO(4, ssbo[4], m_baseGridPacked.size() * sizeof(uint32_t), m_baseGridPacked.data());

    size_t stratGridsSize = kNumStrategies * m_baseGridPacked.size() * sizeof(uint32_t);
    uploadSSBO(5, ssbo[5], stratGridsSize, nullptr);

    size_t placementsSize = kNumStrategies * maxPlacements * sizeof(GpuPlacedBay);
    uploadSSBO(6, ssbo[6], placementsSize, nullptr);

    std::vector<GpuStrategyResult> initResults(kNumStrategies);
    uploadSSBO(7, ssbo[7], kNumStrategies * sizeof(GpuStrategyResult), initResults.data());
    uploadSSBO(8, ssbo[8], gpuTrig.size() * sizeof(GpuTrig), gpuTrig.data());

    struct StratState { int searchFrom; int doneFlag; };
    std::vector<StratState> stateBuf(kNumStrategies);
    for(int i=0; i<kNumStrategies; ++i) { stateBuf[i].searchFrom = 0; stateBuf[i].doneFlag = 0; }
    uploadSSBO(9, ssbo[9], kNumStrategies * sizeof(StratState), stateBuf.data());

    glUseProgram(computeProgram);
    int isFirstRunLoc = glGetUniformLocation(computeProgram, "u_isFirstRun");
    
    int isFirstRun = 1;
    bool allDone = false;
    auto startTime = std::chrono::steady_clock::now();
    
    std::vector<StratState> doneCheckBuf(kNumStrategies);

    while (!allDone) {
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - startTime).count();
        if (elapsed >= 30.0) {
            std::printf("⏱️ TIMEOUT (30s): Deteniendo optimización temprana!\n");
            break;
        }

        glUniform1i(isFirstRunLoc, isFirstRun);
        glDispatchCompute(kNumStrategies, 1, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        
        isFirstRun = 0;
        
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo[9]);
        glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, kNumStrategies * sizeof(StratState), doneCheckBuf.data());
        
        allDone = true;
        for (int i = 0; i < kNumStrategies; ++i) {
            if (doneCheckBuf[i].doneFlag == 0) {
                allDone = false;
                break;
            }
        }
    }

    // Read back results
    std::vector<GpuStrategyResult> finalResults(kNumStrategies);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo[7]);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, finalResults.size() * sizeof(GpuStrategyResult), finalResults.data());

    // Find the best strategy
    int bestIdx = -1;
    double bestQ = std::numeric_limits<double>::infinity();
    for (int i = 0; i < kNumStrategies; ++i) {
        if (finalResults[i].qScore < bestQ && finalResults[i].placementCount > 0) {
            bestQ = finalResults[i].qScore;
            bestIdx = i;
        }
    }

    Result bestRes;
    if (bestIdx != -1) {
        int numPlacements = finalResults[bestIdx].placementCount;
        std::vector<GpuPlacedBay> bestPlacements(numPlacements);
        
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo[6]);
        glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 
                           bestIdx * maxPlacements * sizeof(GpuPlacedBay), 
                           numPlacements * sizeof(GpuPlacedBay), 
                           bestPlacements.data());
        
        for (int i = 0; i < numPlacements; ++i) {
            PlacedBay pb;
            pb.id = bestPlacements[i].id;
            pb.x = bestPlacements[i].x;
            pb.y = bestPlacements[i].y;
            pb.rotation = bestPlacements[i].rotation;
            pb.footprintWidth = bestPlacements[i].footprintWidth;
            pb.footprintDepth = bestPlacements[i].footprintDepth;
            pb.bayWidth = bestPlacements[i].bayWidth;
            pb.bayDepth = bestPlacements[i].bayDepth;
            pb.height = bestPlacements[i].height;
            pb.gap = bestPlacements[i].gap;
            bestRes.placements.push_back(pb);
        }
        
        bestRes.totalPrice = finalResults[bestIdx].totalPrice;
        bestRes.totalLoads = static_cast<long long>(finalResults[bestIdx].totalLoads);
        bestRes.usedArea = finalResults[bestIdx].usedArea;
        bestRes.qScore = finalResults[bestIdx].qScore;
        bestRes.warehouseArea = m_freeStartArea;
        if (m_freeStartArea > 0.0) {
            bestRes.percentageAreaUsed = std::min(1.0, bestRes.usedArea / m_freeStartArea);
        }
    }

    // Cleanup
    glDeleteBuffers(10, ssbo);
    glDeleteProgram(computeProgram);

    return bestRes;
}

} // namespace warehouse

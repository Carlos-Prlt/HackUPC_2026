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

namespace warehouse {

// The OpenGL compute shader string
static const char* kComputeShaderSrc = R"GLSL(
#version 430 core

layout(local_size_x = 256) in;

const uint kFree = 0;
const uint kSolid = 1;
const uint kGap = 2;

struct BayType {
    int id;
    float width;
    float depth;
    float height;
    float gap;
    int nLoads;
    float price;
    float _pad;
};

struct CeilingPoint {
    float x;
    float height;
};

struct PlacedBay {
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

layout(std430, binding = 0) buffer ParamsBuffer {
    float minX;
    float minY;
    float cellSize;
    int gridW;
    int gridH;
    int numBayTypes;
    int numCeilingPoints;
    int maxPlacementsPerStrategy;
    double freeStartArea;
} params;

layout(std430, binding = 1) buffer BayTypesBuffer {
    BayType bayTypes[];
};

layout(std430, binding = 2) buffer RankedBayTypesBuffer {
    int rankedIndices[];
};

layout(std430, binding = 3) buffer CeilingBuffer {
    CeilingPoint ceilingPoints[];
};

layout(std430, binding = 4) buffer BaseGridBuffer {
    uint baseGridPacked[];
};

layout(std430, binding = 5) buffer StrategyGridsBuffer {
    uint strategyGridsPacked[];
};

layout(std430, binding = 6) buffer PlacementsBuffer {
    PlacedBay placements[];
};

struct StrategyResult {
    double totalPrice;
    double totalLoads;
    double usedArea;
    double qScore;
    int placementCount;
    int _pad0;
    int _pad1;
    int _pad2;
};

layout(std430, binding = 7) buffer ResultsBuffer {
    StrategyResult results[];
};

layout(std430, binding = 8) buffer TrigBuffer {
    vec2 trigTable[];
};

struct StratState {
    int searchFrom;
    int doneFlag;
};
layout(std430, binding = 9) buffer StateBuffer {
    StratState states[];
};

uniform int u_isFirstRun;

shared int sharedSearchFrom;
shared bool sharedFoundFreeCell;
shared int sharedGx;
shared int sharedGy;

shared double sharedBestScore;
shared int sharedBestTypeIdx;
shared float sharedBestAngle;

struct OBB {
    float cx[4];
    float cy[4];
};

shared OBB sharedBestSolid;
shared OBB sharedBestGap;
shared bool sharedHasGap;
shared int sharedMaxScore;

float gridToWorldX(int gx) { return params.minX + float(gx) * params.cellSize; }
float gridToWorldY(int gy) { return params.minY + float(gy) * params.cellSize; }

uint getGridCell(uint stratId, int gx, int gy) {
    int idx = gy * params.gridW + gx;
    int wordIdx = idx / 4;
    int byteOffset = (idx % 4) * 8;
    int globalWordIdx = int(stratId) * ((params.gridW * params.gridH + 3) / 4) + wordIdx;
    uint word = strategyGridsPacked[globalWordIdx];
    return (word >> byteOffset) & 0xFFu;
}

void setGridCell(uint stratId, int gx, int gy, uint val) {
    int idx = gy * params.gridW + gx;
    int wordIdx = idx / 4;
    int byteOffset = (idx % 4) * 8;
    int globalWordIdx = int(stratId) * ((params.gridW * params.gridH + 3) / 4) + wordIdx;
    uint mask = ~(0xFFu << byteOffset);
    uint shiftVal = (val & 0xFFu) << byteOffset;
    atomicAnd(strategyGridsPacked[globalWordIdx], mask);
    atomicOr(strategyGridsPacked[globalWordIdx], shiftVal);
}

float ceilingAt(float x) {
    if (params.numCeilingPoints == 0) return 1e30;
    for (int i = 0; i < params.numCeilingPoints; ++i) {
        if (x < ceilingPoints[i].x) {
            if (i == 0) return 1e30;
            return ceilingPoints[i - 1].height;
        }
    }
    return ceilingPoints[params.numCeilingPoints - 1].height;
}

OBB buildOBB(float anchorX, float anchorY, float localW, float localD, float localYOffset, float c, float s) {
    float lx[4] = float[4](0.0, localW, localW, 0.0);
    float ly[4] = float[4](localYOffset, localYOffset, localYOffset + localD, localYOffset + localD);
    OBB obb;
    for (int i = 0; i < 4; ++i) {
        precise float cx = anchorX + lx[i] * c - ly[i] * s;
        precise float cy = anchorY + lx[i] * s + ly[i] * c;
        obb.cx[i] = cx;
        obb.cy[i] = cy;
    }
    return obb;
}

bool obbAabbInGrid(OBB o, out int gx0, out int gy0, out int gx1, out int gy1) {
    float wx0 = o.cx[0], wx1 = o.cx[0];
    float wy0 = o.cy[0], wy1 = o.cy[0];
    for (int i = 1; i < 4; ++i) {
        wx0 = min(wx0, o.cx[i]);
        wx1 = max(wx1, o.cx[i]);
        wy0 = min(wy0, o.cy[i]);
        wy1 = max(wy1, o.cy[i]);
    }
    gx0 = int(floor((wx0 - params.minX) / params.cellSize));
    gy0 = int(floor((wy0 - params.minY) / params.cellSize));
    gx1 = int(ceil((wx1 - params.minX) / params.cellSize));
    gy1 = int(ceil((wy1 - params.minY) / params.cellSize));
    if (gx1 <= 0 || gy1 <= 0 || gx0 >= params.gridW || gy0 >= params.gridH) return false;
    return true;
}

bool cellOverlapsOBB(float cx0, float cy0, float cx1, float cy1, OBB obb) {
    const float kEps = 1e-3;
    float lo, hi;
    lo = obb.cx[0]; hi = obb.cx[0];
    for (int i = 1; i < 4; ++i) { lo = min(lo, obb.cx[i]); hi = max(hi, obb.cx[i]); }
    if (hi <= cx0 + kEps || lo >= cx1 - kEps) return false;
    lo = obb.cy[0]; hi = obb.cy[0];
    for (int i = 1; i < 4; ++i) { lo = min(lo, obb.cy[i]); hi = max(hi, obb.cy[i]); }
    if (hi <= cy0 + kEps || lo >= cy1 - kEps) return false;
    float cellX[4] = float[4](cx0, cx1, cx1, cx0);
    float cellY[4] = float[4](cy0, cy0, cy1, cy1);
    for (int i = 0; i < 2; ++i) {
        int j = (i + 1) & 3;
        precise float nx = -(obb.cy[j] - obb.cy[i]);
        precise float ny =  (obb.cx[j] - obb.cx[i]);
        float oLo = 1e30, oHi = -1e30;
        for (int k = 0; k < 4; ++k) {
            precise float p = obb.cx[k] * nx + obb.cy[k] * ny;
            oLo = min(oLo, p); oHi = max(oHi, p);
        }
        float cLo = 1e30, cHi = -1e30;
        for (int k = 0; k < 4; ++k) {
            precise float p = cellX[k] * nx + cellY[k] * ny;
            cLo = min(cLo, p); cHi = max(cHi, p);
        }
        if (oHi <= cLo + kEps || cHi <= oLo + kEps) return false;
    }
    return true;
}

bool fitsSolidOBB(uint stratId, OBB obb) {
    int gx0, gy0, gx1, gy1;
    if (!obbAabbInGrid(obb, gx0, gy0, gx1, gy1)) return false;
    if (gx0 < 0 || gy0 < 0 || gx1 > params.gridW || gy1 > params.gridH) return false;
    for (int gy = gy0; gy < gy1; ++gy) {
        float wy0 = gridToWorldY(gy);
        float wy1 = wy0 + params.cellSize;
        for (int gx = gx0; gx < gx1; ++gx) {
            float wx0 = gridToWorldX(gx);
            float wx1 = wx0 + params.cellSize;
            if (!cellOverlapsOBB(wx0, wy0, wx1, wy1, obb)) continue;
            if (getGridCell(stratId, gx, gy) != kFree) return false;
        }
    }
    return true;
}

bool fitsGapOBB(uint stratId, OBB obb) {
    int gx0, gy0, gx1, gy1;
    if (!obbAabbInGrid(obb, gx0, gy0, gx1, gy1)) return false;
    if (gx0 < 0 || gy0 < 0 || gx1 > params.gridW || gy1 > params.gridH) return false;
    for (int gy = gy0; gy < gy1; ++gy) {
        float wy0 = gridToWorldY(gy);
        float wy1 = wy0 + params.cellSize;
        for (int gx = gx0; gx < gx1; ++gx) {
            float wx0 = gridToWorldX(gx);
            float wx1 = wx0 + params.cellSize;
            if (!cellOverlapsOBB(wx0, wy0, wx1, wy1, obb)) continue;
            if (getGridCell(stratId, gx, gy) == kSolid) return false;
        }
    }
    return true;
}

void markSolidOBB(uint stratId, OBB obb) {
    int gx0, gy0, gx1, gy1;
    if (!obbAabbInGrid(obb, gx0, gy0, gx1, gy1)) return;
    gx0 = max(gx0, 0); gy0 = max(gy0, 0);
    gx1 = min(gx1, params.gridW); gy1 = min(gy1, params.gridH);
    for (int gy = gy0; gy < gy1; ++gy) {
        float wy0 = gridToWorldY(gy);
        float wy1 = wy0 + params.cellSize;
        for (int gx = gx0; gx < gx1; ++gx) {
            float wx0 = gridToWorldX(gx);
            float wx1 = wx0 + params.cellSize;
            if (cellOverlapsOBB(wx0, wy0, wx1, wy1, obb)) {
                setGridCell(stratId, gx, gy, kSolid);
            }
        }
    }
}

void markGapOBB(uint stratId, OBB obb) {
    int gx0, gy0, gx1, gy1;
    if (!obbAabbInGrid(obb, gx0, gy0, gx1, gy1)) return;
    gx0 = max(gx0, 0); gy0 = max(gy0, 0);
    gx1 = min(gx1, params.gridW); gy1 = min(gy1, params.gridH);
    for (int gy = gy0; gy < gy1; ++gy) {
        float wy0 = gridToWorldY(gy);
        float wy1 = wy0 + params.cellSize;
        for (int gx = gx0; gx < gx1; ++gx) {
            float wx0 = gridToWorldX(gx);
            float wx1 = wx0 + params.cellSize;
            if (cellOverlapsOBB(wx0, wy0, wx1, wy1, obb)) {
                uint c = getGridCell(stratId, gx, gy);
                if (c == kFree) {
                    setGridCell(stratId, gx, gy, kGap);
                }
            }
        }
    }
}

bool ceilingClearsOBB(OBB obb, float bayHeight) {
    float wx0 = obb.cx[0], wx1 = obb.cx[0];
    for (int i = 1; i < 4; ++i) {
        wx0 = min(wx0, obb.cx[i]);
        wx1 = max(wx1, obb.cx[i]);
    }
    if (wx0 > wx1) { float t = wx0; wx0 = wx1; wx1 = t; }
    for (float x = wx0; x <= wx1; x += params.cellSize) {
        if (ceilingAt(x) < bayHeight) return false;
    }
    if (ceilingAt(wx1) < bayHeight) return false;
    return true;
}

void main() {
    uint stratId = gl_WorkGroupID.x;
    uint tid = gl_LocalInvocationID.x;
    
    // We have 144 strategies: 9 rank modes * 8 sweep dirs * 2 angle offsets.
    // stratId = (rankMode * 16) + (sweepDir * 2) + angleShiftIndex
    int sweepDir = int((stratId / 2) % 8); 
    int angleShiftIndex = int(stratId % 2);
    
    int angleOffset = 0;
    if (sweepDir == 0) angleOffset = 0;
    else if (sweepDir == 1) angleOffset = 180;
    else if (sweepDir == 2) angleOffset = 270;
    else if (sweepDir == 3) angleOffset = 90;
    else if (sweepDir == 4) angleOffset = 0;
    else if (sweepDir == 5) angleOffset = 180;
    else if (sweepDir == 6) angleOffset = 270;
    else if (sweepDir == 7) angleOffset = 90;
    
    if (angleShiftIndex == 1) angleOffset += 45;

    if (u_isFirstRun == 1) {
        if (tid == 0) {
            if (sweepDir == 0 || sweepDir == 3 || sweepDir == 4 || sweepDir == 6) {
                states[stratId].searchFrom = 0;
            } else {
                if (sweepDir < 4) states[stratId].searchFrom = params.gridH - 1;
                else states[stratId].searchFrom = params.gridW - 1;
            }
            states[stratId].doneFlag = 0;
            
            results[stratId].totalPrice = 0.0;
            results[stratId].totalLoads = 0.0;
            results[stratId].usedArea = 0.0;
            results[stratId].qScore = 1e30;
            results[stratId].placementCount = 0;
        }
        
        int totalWords = (params.gridW * params.gridH + 3) / 4;
        for (int i = int(tid); i < totalWords; i += 256) {
            strategyGridsPacked[stratId * totalWords + i] = baseGridPacked[i];
        }
        return; // First run only initializes
    }
    
    if (states[stratId].doneFlag == 1) return;

    barrier();

    const float kPi = 3.14159265358979323846;

    if (tid == 0) {
        sharedSearchFrom = states[stratId].searchFrom;
        sharedFoundFreeCell = false;
        
        // Row-major sweeps (0-3)
        if (sweepDir == 0) {
            for (int y = sharedSearchFrom; y < params.gridH && !sharedFoundFreeCell; ++y) {
                for (int x = 0; x < params.gridW; ++x) {
                    if (getGridCell(stratId, x, y) == kFree) {
                        sharedGx = x; sharedGy = y; sharedFoundFreeCell = true; break;
                    }
                }
            }
        } else if (sweepDir == 1) {
            for (int y = sharedSearchFrom; y >= 0 && !sharedFoundFreeCell; --y) {
                for (int x = params.gridW - 1; x >= 0; --x) {
                    if (getGridCell(stratId, x, y) == kFree) {
                        sharedGx = x; sharedGy = y; sharedFoundFreeCell = true; break;
                    }
                }
            }
        } else if (sweepDir == 2) {
            for (int y = sharedSearchFrom; y >= 0 && !sharedFoundFreeCell; --y) {
                for (int x = 0; x < params.gridW; ++x) {
                    if (getGridCell(stratId, x, y) == kFree) {
                        sharedGx = x; sharedGy = y; sharedFoundFreeCell = true; break;
                    }
                }
            }
        } else if (sweepDir == 3) {
            for (int y = sharedSearchFrom; y < params.gridH && !sharedFoundFreeCell; ++y) {
                for (int x = params.gridW - 1; x >= 0; --x) {
                    if (getGridCell(stratId, x, y) == kFree) {
                        sharedGx = x; sharedGy = y; sharedFoundFreeCell = true; break;
                    }
                }
            }
        }
        // Column-major sweeps (4-7)
        else if (sweepDir == 4) {
            for (int x = sharedSearchFrom; x < params.gridW && !sharedFoundFreeCell; ++x) {
                for (int y = 0; y < params.gridH; ++y) {
                    if (getGridCell(stratId, x, y) == kFree) {
                        sharedGx = x; sharedGy = y; sharedFoundFreeCell = true; break;
                    }
                }
            }
        } else if (sweepDir == 5) {
            for (int x = sharedSearchFrom; x >= 0 && !sharedFoundFreeCell; --x) {
                for (int y = params.gridH - 1; y >= 0; --y) {
                    if (getGridCell(stratId, x, y) == kFree) {
                        sharedGx = x; sharedGy = y; sharedFoundFreeCell = true; break;
                    }
                }
            }
        } else if (sweepDir == 6) {
            for (int x = sharedSearchFrom; x >= 0 && !sharedFoundFreeCell; --x) {
                for (int y = 0; y < params.gridH; ++y) {
                    if (getGridCell(stratId, x, y) == kFree) {
                        sharedGx = x; sharedGy = y; sharedFoundFreeCell = true; break;
                    }
                }
            }
        } else if (sweepDir == 7) {
            for (int x = sharedSearchFrom; x < params.gridW && !sharedFoundFreeCell; ++x) {
                for (int y = params.gridH - 1; y >= 0; --y) {
                    if (getGridCell(stratId, x, y) == kFree) {
                        sharedGx = x; sharedGy = y; sharedFoundFreeCell = true; break;
                    }
                }
            }
        }
        
        sharedBestScore = -1.0;
        sharedBestTypeIdx = -1;
    }
    barrier();

    if (!sharedFoundFreeCell) {
        if (tid == 0) states[stratId].doneFlag = 1;
        return;
    }

        float anchorX = gridToWorldX(sharedGx);
        float anchorY = gridToWorldY(sharedGy);

        int totalChecks = params.numBayTypes * 360;
        
        double myBestScore = -1.0;
        int myBestTypeIdx = -1;
        float myBestAngle = 0.0;
        OBB myBestSolid;
        OBB myBestGap;
        bool myHasGap = false;

        for (int i = int(tid); i < totalChecks; i += 256) {
            int rankOrderIdx = i / 360;
            int typeIdx = rankedIndices[stratId * params.numBayTypes + rankOrderIdx];
            int ai = i % 360;
            int di = (ai + angleOffset) % 360;
            
            float deg = float(di);
            float c = trigTable[di].x;
            float s = trigTable[di].y;

            BayType type = bayTypes[typeIdx];
            
            OBB solid = buildOBB(anchorX, anchorY, type.width, type.depth, 0.0, c, s);
            if (!fitsSolidOBB(stratId, solid)) continue;
            if (!ceilingClearsOBB(solid, type.height)) continue;

            OBB gap;
            bool gapPresent = (type.gap > 0.0);
            if (gapPresent) {
                gap = buildOBB(anchorX, anchorY, type.width, type.gap, type.depth, c, s);
                if (!fitsGapOBB(stratId, gap)) continue;
            }

            double score = 1000000.0 - double(rankOrderIdx * 360 + ai);
            if (score > myBestScore) {
                myBestScore = score;
                myBestTypeIdx = typeIdx;
                myBestAngle = deg;
                myBestSolid = solid;
                myBestGap = gap;
                myHasGap = gapPresent;
            }
        }

        int intScore = int(myBestScore);
        if (tid == 0) sharedMaxScore = -1;
        barrier();

        if (intScore > -1) {
            atomicMax(sharedMaxScore, intScore);
        }
        barrier();

        if (intScore > -1 && intScore == sharedMaxScore) {
            sharedBestTypeIdx = myBestTypeIdx;
            sharedBestAngle = myBestAngle;
            sharedBestSolid = myBestSolid;
            sharedBestGap = myBestGap;
            sharedHasGap = myHasGap;
        }
        barrier();

    if (tid == 0) {
        if (sharedBestTypeIdx == -1) {
            setGridCell(stratId, sharedGx, sharedGy, kSolid);
        } else {
            int count = results[stratId].placementCount;
            if (count < params.maxPlacementsPerStrategy) {
                BayType btype = bayTypes[sharedBestTypeIdx];
                
                float fxMin = sharedBestSolid.cx[0], fxMax = sharedBestSolid.cx[0];
                float fyMin = sharedBestSolid.cy[0], fyMax = sharedBestSolid.cy[0];
                for (int i = 1; i < 4; ++i) {
                    fxMin = min(fxMin, sharedBestSolid.cx[i]);
                    fxMax = max(fxMax, sharedBestSolid.cx[i]);
                    fyMin = min(fyMin, sharedBestSolid.cy[i]);
                    fyMax = max(fyMax, sharedBestSolid.cy[i]);
                }

                PlacedBay pb;
                pb.id = btype.id;
                pb.x = anchorX;
                pb.y = anchorY;
                pb.rotation = sharedBestAngle;
                pb.bayWidth = btype.width;
                pb.bayDepth = btype.depth;
                pb.height = btype.height;
                pb.gap = btype.gap;
                pb.footprintWidth = fxMax - fxMin;
                pb.footprintDepth = fyMax - fyMin;
                
                placements[stratId * params.maxPlacementsPerStrategy + count] = pb;
                results[stratId].placementCount = count + 1;

                results[stratId].totalPrice += btype.price;
                results[stratId].totalLoads += double(btype.nLoads);
                results[stratId].usedArea += double(btype.width) * double(btype.depth);

                markSolidOBB(stratId, sharedBestSolid);
                if (sharedHasGap) {
                    markGapOBB(stratId, sharedBestGap);
                }
            } else {
                setGridCell(stratId, sharedGx, sharedGy, kSolid);
                states[stratId].doneFlag = 1;
            }
        }
        if (sweepDir < 4) states[stratId].searchFrom = sharedGy;
        else states[stratId].searchFrom = sharedGx;
        
        // Compute Q score periodically or at the end
        double whArea = params.freeStartArea;
        double pctArea = min(1.0, results[stratId].usedArea / whArea);
        if (results[stratId].totalLoads > 0.0) {
            double base = results[stratId].totalPrice / results[stratId].totalLoads;
            double exp = 2.0 - pctArea;
            results[stratId].qScore = double(pow(float(base), float(exp)));
        } else {
            results[stratId].qScore = 1e30;
        }
    }
}
)GLSL";

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
    unsigned int shader = glCreateShader(GL_COMPUTE_SHADER);
    glShaderSource(shader, 1, &kComputeShaderSrc, nullptr);
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

    static constexpr int kNumStrategies = 144;
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

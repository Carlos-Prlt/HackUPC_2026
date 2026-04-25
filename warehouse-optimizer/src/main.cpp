// =============================================================================
//  main.cpp
//  Top-level driver. Walks through the four pipeline modules in order:
//
//      1. Data Input    -> DataLoader::load
//      2. Processing    -> Optimizer::solve   (prints "ID, X, Y, Rotation")
//      3. Data Output   -> SceneBuilder::build
//      4. Visualization -> Application::init + main loop
//
//  CLI:
//      warehouse_optimizer [data_dir]                 (default: ./data)
//      warehouse_optimizer --no-window [data_dir]     (skip the renderer)
//
//  Emscripten build: the main loop is driven through emscripten_set_main_loop
//  so that the page can keep ticking the renderer cooperatively.
// =============================================================================
#include "input/DataLoader.hpp"
#include "processing/Optimizer.hpp"
#include "output/Scene.hpp"
#include "visualization/Application.hpp"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#if defined(__EMSCRIPTEN__)
#  include <emscripten.h>
#endif

namespace {

void printSolverHeader() {
    std::cout << "ID, X, Y, Rotation\n";
}

void printSolution(const whopt::OptimizerResult& r) {
    // Required output: "ID, X, Y, Rotation" per placed bay.
    for (const auto& b : r.bays) {
        std::cout << b.typeId << ", "
                  << b.x << ", "
                  << b.y << ", "
                  << b.rotationDeg << "\n";
    }
}

void printSummary(const whopt::OptimizerResult& r) {
    std::cerr << "----------------------------------------------------\n"
              << "Bays placed              : " << r.bays.size()             << "\n"
              << "Total price (Σ price)    : " << r.totalPrice              << "\n"
              << "Total loads (Σ nLoads)   : " << r.totalLoads              << "\n"
              << "Used / Warehouse area    : " << r.usedArea << " / " << r.warehouseArea << "\n"
              << "Percentage area used     : " << (r.percentageAreaUsed * 100.0) << " %\n"
              << "Q (objective, lower=bett.): " << r.Q                      << "\n"
              << "----------------------------------------------------\n";
}

#if defined(__EMSCRIPTEN__)
std::unique_ptr<whopt::Application> g_app;
void emTick() { if (g_app) g_app->tick(); }
#endif

} // anon

int main(int argc, char** argv) {
    std::string dataDir = "data";
    bool showWindow = true;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--no-window")        showWindow = false;
        else if (a == "-h" || a == "--help") {
            std::cout << "Usage: " << argv[0] << " [--no-window] [data_dir]\n";
            return 0;
        } else {
            dataDir = a;
        }
    }

    try {
        // -------- 1. DATA INPUT --------------------------------------------
        whopt::Dataset data = whopt::DataLoader::load(dataDir);

        // -------- 2. PROCESSING --------------------------------------------
        whopt::Optimizer optimizer(data);
        whopt::OptimizerResult result = optimizer.solve();

        printSolverHeader();
        printSolution(result);
        printSummary(result);

        if (!showWindow) return 0;

        // -------- 3. DATA OUTPUT (renderer-ready scene) --------------------
        whopt::Scene scene = whopt::SceneBuilder::build(data, result);

        // -------- 4. VISUALIZATION -----------------------------------------
        whopt::Application::Config cfg;
        cfg.title = "Warehouse Optimizer";

#if defined(__EMSCRIPTEN__)
        g_app = std::make_unique<whopt::Application>();
        if (!g_app->init(cfg, std::move(scene))) return 1;
        emscripten_set_main_loop(emTick, 0, 1);     // 0 = use rAF
        // unreachable in Emscripten simulate-infinite-loop mode
        return 0;
#else
        whopt::Application app;
        if (!app.init(cfg, std::move(scene))) return 1;
        app.runDesktop();
        app.shutdown();
        return 0;
#endif
    }
    catch (const std::exception& ex) {
        std::cerr << "[FATAL] " << ex.what() << "\n";
        return 2;
    }
}

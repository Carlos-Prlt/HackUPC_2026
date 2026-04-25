// =============================================================================
//  DataLoader.hpp
//  Data ingestion module: reads the four input CSV files and turns them into
//  the populated Dataset bundle defined in core/Domain.hpp.
//
//  Format reminders:
//    WAREHOUSE.CSV       :: x, y                            (one polygon vertex per row)
//    OBSTACLES.CSV       :: x, y, width, depth              (one rectangle per row)
//    CEILING.CSV         :: x, height                       (piecewise-constant profile)
//    TYPES_OF_BAYS.CSV   :: id, width, depth, height,
//                          gap, nLoads, price               (one bay catalogue row)
//
//  Header rows are auto-detected (any non-numeric leading row is skipped).
// =============================================================================
#pragma once

#include "core/Domain.hpp"
#include <string>
#include <vector>

namespace whopt {

class CSVParser {
public:
    // Reads `path` and returns its rows. Each row is a vector of trimmed cells.
    // Lines that are blank or that start with '#' are skipped. Quoted cells
    // are supported (double-quote escape). Throws std::runtime_error on I/O
    // failure.
    static std::vector<std::vector<std::string>> read(const std::string& path);

    // Helpers to convert a cell while throwing a descriptive error containing
    // the file/row context.
    static double parseDouble(const std::string& cell, const std::string& ctx);
    static int    parseInt   (const std::string& cell, const std::string& ctx);
};

class DataLoader {
public:
    // Loads all four CSVs and returns a fully populated Dataset.
    // `dataDir` is the directory containing WAREHOUSE.CSV, OBSTACLES.CSV,
    // CEILING.CSV and TYPES_OF_BAYS.CSV. Pass "." to read from cwd.
    static Dataset load(const std::string& dataDir);

    // Individual loaders, also useful for unit tests.
    static Warehouse              loadWarehouse(const std::string& path);
    static std::vector<Obstacle>  loadObstacles(const std::string& path);
    static CeilingProfile         loadCeiling  (const std::string& path);
    static std::vector<BayType>   loadBayTypes (const std::string& path);
};

} // namespace whopt

// =============================================================================
//  DataLoader.cpp
// =============================================================================
#include "input/DataLoader.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>

namespace whopt {

// ---------- small string helpers --------------------------------------------

static std::string trim(const std::string& s) {
    auto begin = s.begin();
    while (begin != s.end() && std::isspace(static_cast<unsigned char>(*begin))) ++begin;
    auto end = s.end();
    while (end != begin && std::isspace(static_cast<unsigned char>(*(end - 1)))) --end;
    return std::string(begin, end);
}

static bool looksNumeric(const std::string& s) {
    if (s.empty()) return false;
    bool sawDigit = false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (i == 0 && (c == '+' || c == '-')) continue;
        if (c == '.' || c == 'e' || c == 'E' || c == ',') continue;
        if (std::isdigit(static_cast<unsigned char>(c))) { sawDigit = true; continue; }
        return false;
    }
    return sawDigit;
}

// ---------- CSVParser --------------------------------------------------------

std::vector<std::vector<std::string>> CSVParser::read(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("CSVParser: cannot open file '" + path + "'");
    }
    std::vector<std::vector<std::string>> rows;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;

        std::vector<std::string> row;
        std::string cell;
        bool inQuotes = false;
        for (std::size_t i = 0; i < line.size(); ++i) {
            char c = line[i];
            if (inQuotes) {
                if (c == '"') {
                    if (i + 1 < line.size() && line[i + 1] == '"') { cell.push_back('"'); ++i; }
                    else                                            { inQuotes = false; }
                } else {
                    cell.push_back(c);
                }
            } else {
                if      (c == ',')  { row.push_back(trim(cell)); cell.clear(); }
                else if (c == '"')  { inQuotes = true; }
                else if (c == ';')  { row.push_back(trim(cell)); cell.clear(); }
                else                { cell.push_back(c); }
            }
        }
        row.push_back(trim(cell));
        rows.push_back(std::move(row));
    }
    return rows;
}

double CSVParser::parseDouble(const std::string& cell, const std::string& ctx) {
    try {
        std::string s = cell;
        // Tolerate European decimal commas like "1,5" -> "1.5", but only when
        // there is no dot already.
        if (s.find('.') == std::string::npos) {
            std::replace(s.begin(), s.end(), ',', '.');
        }
        std::size_t pos = 0;
        double v = std::stod(s, &pos);
        if (pos == 0) throw std::invalid_argument("no digits");
        return v;
    } catch (const std::exception& e) {
        throw std::runtime_error("CSVParser: invalid number '" + cell + "' (" + ctx + "): " + e.what());
    }
}

int CSVParser::parseInt(const std::string& cell, const std::string& ctx) {
    try {
        std::size_t pos = 0;
        int v = std::stoi(cell, &pos);
        if (pos == 0) throw std::invalid_argument("no digits");
        return v;
    } catch (const std::exception& e) {
        throw std::runtime_error("CSVParser: invalid integer '" + cell + "' (" + ctx + "): " + e.what());
    }
}

// ---------- per-file loaders -------------------------------------------------

static std::size_t firstDataRow(const std::vector<std::vector<std::string>>& rows) {
    if (rows.empty()) return 0;
    // Skip the very first row only if it does not look numeric (header detection).
    return (rows[0].empty() || !looksNumeric(rows[0][0])) ? 1u : 0u;
}

Warehouse DataLoader::loadWarehouse(const std::string& path) {
    auto rows = CSVParser::read(path);
    Warehouse wh;
    for (std::size_t i = firstDataRow(rows); i < rows.size(); ++i) {
        const auto& r = rows[i];
        if (r.size() < 2) {
            throw std::runtime_error("WAREHOUSE.CSV row " + std::to_string(i + 1) + ": expected 2 columns");
        }
        Point2 p;
        p.x = CSVParser::parseDouble(r[0], "WAREHOUSE.CSV row " + std::to_string(i + 1) + " col 1");
        p.y = CSVParser::parseDouble(r[1], "WAREHOUSE.CSV row " + std::to_string(i + 1) + " col 2");
        wh.vertices.push_back(p);
    }
    if (wh.vertices.size() < 3) {
        throw std::runtime_error("WAREHOUSE.CSV: need at least 3 vertices, got "
                                 + std::to_string(wh.vertices.size()));
    }
    // Drop a trailing duplicate of the first vertex if the file closes the loop.
    if (wh.vertices.size() >= 2) {
        const auto& f = wh.vertices.front();
        const auto& b = wh.vertices.back();
        if (std::fabs(f.x - b.x) < 1e-9 && std::fabs(f.y - b.y) < 1e-9) {
            wh.vertices.pop_back();
        }
    }
    return wh;
}

std::vector<Obstacle> DataLoader::loadObstacles(const std::string& path) {
    std::vector<Obstacle> obs;
    if (!std::filesystem::exists(path)) return obs;     // obstacles file is optional
    auto rows = CSVParser::read(path);
    for (std::size_t i = firstDataRow(rows); i < rows.size(); ++i) {
        const auto& r = rows[i];
        if (r.size() < 4) {
            throw std::runtime_error("OBSTACLES.CSV row " + std::to_string(i + 1) + ": expected 4 columns");
        }
        Obstacle o;
        const std::string ctx = "OBSTACLES.CSV row " + std::to_string(i + 1);
        o.rect.x = CSVParser::parseDouble(r[0], ctx + " col 1");
        o.rect.y = CSVParser::parseDouble(r[1], ctx + " col 2");
        o.rect.w = CSVParser::parseDouble(r[2], ctx + " col 3");
        o.rect.d = CSVParser::parseDouble(r[3], ctx + " col 4");
        obs.push_back(o);
    }
    return obs;
}

CeilingProfile DataLoader::loadCeiling(const std::string& path) {
    auto rows = CSVParser::read(path);
    CeilingProfile prof;
    for (std::size_t i = firstDataRow(rows); i < rows.size(); ++i) {
        const auto& r = rows[i];
        if (r.size() < 2) {
            throw std::runtime_error("CEILING.CSV row " + std::to_string(i + 1) + ": expected 2 columns");
        }
        CeilingSample s;
        const std::string ctx = "CEILING.CSV row " + std::to_string(i + 1);
        s.x      = CSVParser::parseDouble(r[0], ctx + " col 1");
        s.height = CSVParser::parseDouble(r[1], ctx + " col 2");
        prof.samples.push_back(s);
    }
    std::sort(prof.samples.begin(), prof.samples.end(),
              [](const CeilingSample& a, const CeilingSample& b) { return a.x < b.x; });
    return prof;
}

std::vector<BayType> DataLoader::loadBayTypes(const std::string& path) {
    auto rows = CSVParser::read(path);
    std::vector<BayType> types;
    for (std::size_t i = firstDataRow(rows); i < rows.size(); ++i) {
        const auto& r = rows[i];
        if (r.size() < 7) {
            throw std::runtime_error("TYPES_OF_BAYS.CSV row " + std::to_string(i + 1) + ": expected 7 columns");
        }
        const std::string ctx = "TYPES_OF_BAYS.CSV row " + std::to_string(i + 1);
        BayType t;
        t.id     = r[0];
        t.width  = CSVParser::parseDouble(r[1], ctx + " col 2");
        t.depth  = CSVParser::parseDouble(r[2], ctx + " col 3");
        t.height = CSVParser::parseDouble(r[3], ctx + " col 4");
        t.gap    = CSVParser::parseDouble(r[4], ctx + " col 5");
        t.nLoads = CSVParser::parseInt   (r[5], ctx + " col 6");
        t.price  = CSVParser::parseDouble(r[6], ctx + " col 7");
        if (t.width <= 0 || t.depth <= 0 || t.height <= 0 || t.nLoads <= 0 || t.price <= 0) {
            throw std::runtime_error(ctx + ": all numeric fields must be positive");
        }
        types.push_back(std::move(t));
    }
    return types;
}

Dataset DataLoader::load(const std::string& dataDir) {
    namespace fs = std::filesystem;
    auto join = [&](const char* name) { return (fs::path(dataDir) / name).string(); };
    Dataset d;
    d.warehouse = loadWarehouse (join("WAREHOUSE.CSV"));
    d.obstacles = loadObstacles (join("OBSTACLES.CSV"));
    d.ceiling   = loadCeiling   (join("CEILING.CSV"));
    d.bayTypes  = loadBayTypes  (join("TYPES_OF_BAYS.CSV"));
    return d;
}

} // namespace whopt

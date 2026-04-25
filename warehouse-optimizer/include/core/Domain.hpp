// =============================================================================
//  Domain.hpp
//  Core domain entities for the warehouse optimizer.
//
//  Everything parsed from CSV maps onto these structs. They are deliberately
//  POD-like so they can be passed around freely between modules.
// =============================================================================
#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <algorithm>

namespace whopt {

// ----- Geometry primitives ---------------------------------------------------

struct Point2 {
    double x = 0.0;
    double y = 0.0;
};

struct Rect {
    double x = 0.0;     // bottom-left
    double y = 0.0;
    double w = 0.0;
    double d = 0.0;     // depth (Y extent)

    [[nodiscard]] double minX() const { return x; }
    [[nodiscard]] double maxX() const { return x + w; }
    [[nodiscard]] double minY() const { return y; }
    [[nodiscard]] double maxY() const { return y + d; }
    [[nodiscard]] double area() const { return w * d; }

    // Strict overlap. Bays are allowed to share boundaries with each other
    // and with walls, so an edge-touch must NOT count as overlap.
    [[nodiscard]] bool overlaps(const Rect& other) const {
        return  minX() < other.maxX() && maxX() > other.minX()
             && minY() < other.maxY() && maxY() > other.minY();
    }
};

// ----- Warehouse perimeter ---------------------------------------------------
//
// Walls are axis-aligned, so the perimeter is a rectilinear polygon stored as
// an ordered list of vertices (closed loop, last == first is optional).

struct Warehouse {
    std::vector<Point2> vertices;

    [[nodiscard]] Rect boundingBox() const {
        Rect r;
        if (vertices.empty()) return r;
        double minx = vertices[0].x, maxx = vertices[0].x;
        double miny = vertices[0].y, maxy = vertices[0].y;
        for (const auto& v : vertices) {
            minx = std::min(minx, v.x); maxx = std::max(maxx, v.x);
            miny = std::min(miny, v.y); maxy = std::max(maxy, v.y);
        }
        r.x = minx; r.y = miny; r.w = maxx - minx; r.d = maxy - miny;
        return r;
    }

    // Ray-cast point-in-polygon. Boundary points are considered inside.
    [[nodiscard]] bool contains(const Point2& p, double eps = 1e-6) const;

    // True iff the entire axis-aligned rectangle is inside the polygon
    // (including boundary touches).
    [[nodiscard]] bool containsRect(const Rect& r, double eps = 1e-6) const;

    [[nodiscard]] double area() const;
};

// ----- Obstacles -------------------------------------------------------------

struct Obstacle {
    Rect rect;
};

// ----- Ceiling profile -------------------------------------------------------
//
// The ceiling is described as a piecewise-constant (step) profile along the
// X-axis. A sample (x, h) means "from this x onward the ceiling height is h
// until the next sample". Samples must be sorted ascending in x.

struct CeilingSample {
    double x = 0.0;
    double height = 0.0;
};

struct CeilingProfile {
    std::vector<CeilingSample> samples;

    // Returns minimum ceiling height across the interval [x0, x1].
    [[nodiscard]] double minHeightInRange(double x0, double x1) const;

    [[nodiscard]] double maxHeight() const {
        double m = 0.0;
        for (const auto& s : samples) m = std::max(m, s.height);
        return m;
    }
};

// ----- Bay catalogue ---------------------------------------------------------
//
// A BayType is a row in TYPES_OF_BAYS.CSV. A Bay is a placed instance of a
// BayType (id + position + rotation in degrees: 0 or 90).

struct BayType {
    std::string id;
    double width  = 0.0;   // X extent at 0 deg
    double depth  = 0.0;   // Y extent at 0 deg
    double height = 0.0;
    double gap    = 0.0;   // service gap required around the bay (kept on Y side)
    int    nLoads = 0;     // load capacity
    double price  = 0.0;
};

struct Bay {
    std::string typeId;
    double x = 0.0;        // bottom-left of physical footprint
    double y = 0.0;
    int    rotationDeg = 0;  // 0 or 90
    // Cached footprint (already accounts for rotation, NOT for gap).
    double width  = 0.0;
    double depth  = 0.0;
    double height = 0.0;
    int    nLoads = 0;
    double price  = 0.0;
};

// ----- Top-level dataset bundled by DataLoader -------------------------------

struct Dataset {
    Warehouse              warehouse;
    std::vector<Obstacle>  obstacles;
    CeilingProfile         ceiling;
    std::vector<BayType>   bayTypes;
};

} // namespace whopt

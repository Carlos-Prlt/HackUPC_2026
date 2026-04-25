// =============================================================================
//  Domain.cpp
//  Geometric queries used by the rest of the pipeline.
// =============================================================================
#include "core/Domain.hpp"

#include <cmath>
#include <algorithm>

namespace whopt {

// -----------------------------------------------------------------------------
// Point-in-polygon (ray casting). Boundary points count as inside, which is
// important because bays may share edges with the warehouse walls.
// -----------------------------------------------------------------------------
bool Warehouse::contains(const Point2& p, double eps) const {
    if (vertices.size() < 3) return false;
    const std::size_t n = vertices.size();

    // Boundary check first: lying on any edge counts as inside.
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const Point2& a = vertices[j];
        const Point2& b = vertices[i];
        const double minx = std::min(a.x, b.x), maxx = std::max(a.x, b.x);
        const double miny = std::min(a.y, b.y), maxy = std::max(a.y, b.y);
        if (p.x + eps < minx || p.x - eps > maxx) continue;
        if (p.y + eps < miny || p.y - eps > maxy) continue;
        const double cross = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
        if (std::fabs(cross) <= eps * std::max(1.0, std::fabs(b.x - a.x) + std::fabs(b.y - a.y))) {
            return true;
        }
    }

    bool inside = false;
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const Point2& a = vertices[i];
        const Point2& b = vertices[j];
        const bool yCross = (a.y > p.y) != (b.y > p.y);
        if (yCross) {
            const double xIntersect = (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x;
            if (p.x < xIntersect) inside = !inside;
        }
    }
    return inside;
}

// -----------------------------------------------------------------------------
// Rectangle-in-polygon test for rectilinear warehouses.
//
// For the axis-aligned warehouses promised by the spec it is sufficient to:
//   1. confirm the four corners and the centroid are inside the polygon, and
//   2. confirm no polygon edge crosses the rectangle's interior.
//
// (1) alone is not enough for general non-convex polygons; (2) handles the
// concavity case where corners are inside but a wall slices through the rect.
// -----------------------------------------------------------------------------
static bool segmentsCross(const Point2& a, const Point2& b,
                          const Point2& c, const Point2& d, double eps)
{
    auto sign = [](double v, double e) {
        if (v >  e) return  1;
        if (v < -e) return -1;
        return 0;
    };
    const double d1 = (d.x - c.x) * (a.y - c.y) - (d.y - c.y) * (a.x - c.x);
    const double d2 = (d.x - c.x) * (b.y - c.y) - (d.y - c.y) * (b.x - c.x);
    const double d3 = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    const double d4 = (b.x - a.x) * (d.y - a.y) - (b.y - a.y) * (d.x - a.x);
    return sign(d1, eps) * sign(d2, eps) < 0 &&
           sign(d3, eps) * sign(d4, eps) < 0;
}

bool Warehouse::containsRect(const Rect& r, double eps) const {
    const Point2 corners[4] = {
        {r.minX(), r.minY()},
        {r.maxX(), r.minY()},
        {r.maxX(), r.maxY()},
        {r.minX(), r.maxY()},
    };
    for (const auto& c : corners) if (!contains(c, eps)) return false;
    if (!contains({(r.minX() + r.maxX()) * 0.5, (r.minY() + r.maxY()) * 0.5}, eps)) return false;

    // No polygon edge may slice across rectangle interior.
    const std::size_t n = vertices.size();
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const Point2& a = vertices[j];
        const Point2& b = vertices[i];
        for (int k = 0; k < 4; ++k) {
            const Point2& p1 = corners[k];
            const Point2& p2 = corners[(k + 1) & 3];
            if (segmentsCross(a, b, p1, p2, eps)) return false;
        }
    }
    return true;
}

double Warehouse::area() const {
    if (vertices.size() < 3) return 0.0;
    double a = 0.0;
    const std::size_t n = vertices.size();
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        a += (vertices[j].x + vertices[i].x) * (vertices[i].y - vertices[j].y);
    }
    return std::fabs(a) * 0.5;
}

// -----------------------------------------------------------------------------
// Ceiling: piecewise-constant (right-continuous) profile.
// -----------------------------------------------------------------------------
double CeilingProfile::minHeightInRange(double x0, double x1) const {
    if (samples.empty()) return 0.0;
    if (x1 < x0) std::swap(x0, x1);

    double minH = std::numeric_limits<double>::infinity();

    // Locate the segment containing x0.
    std::size_t i = 0;
    while (i + 1 < samples.size() && samples[i + 1].x <= x0) ++i;

    // Segment i covers [samples[i].x, samples[i+1].x) (or to +inf for last).
    while (i < samples.size()) {
        const double segStart = samples[i].x;
        const double segEnd = (i + 1 < samples.size())
                             ? samples[i + 1].x
                             : std::numeric_limits<double>::infinity();
        if (segStart > x1) break;
        const double overlapStart = std::max(segStart, x0);
        const double overlapEnd   = std::min(segEnd,   x1);
        if (overlapEnd > overlapStart || (overlapEnd == overlapStart && i == samples.size() - 1)) {
            minH = std::min(minH, samples[i].height);
        }
        ++i;
    }

    if (!std::isfinite(minH)) {
        // x range fell entirely before the first sample; treat first sample as
        // the leftmost height. This is forgiving rather than fatal.
        minH = samples.front().height;
    }
    return minH;
}

} // namespace whopt

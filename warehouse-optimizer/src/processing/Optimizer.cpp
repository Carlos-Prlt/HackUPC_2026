// =============================================================================
//  Optimizer.cpp
//  Greedy bottom-left-fill solver with multi-strategy ranking.
// =============================================================================
#include "processing/Optimizer.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <set>

namespace whopt {

// ----------------------------------------------------------------------------
// Q metric. We compute it the same way the spec defines it. When loads or
// area are zero we deliberately return +infinity so that any feasible
// placement automatically beats the empty placement.
// ----------------------------------------------------------------------------
static double computeQ(double totalPrice, long long totalLoads, double pctArea) {
    if (totalLoads <= 0 || pctArea <= 0.0) {
        return std::numeric_limits<double>::infinity();
    }
    const double base = totalPrice / static_cast<double>(totalLoads);
    const double exponent = 2.0 - pctArea;
    return std::pow(base, exponent);
}

void Optimizer::recomputeMetrics(OptimizerResult& r, double warehouseArea) {
    r.totalPrice = 0.0;
    r.totalLoads = 0;
    r.usedArea   = 0.0;
    for (const auto& b : r.bays) {
        r.totalPrice += b.price;
        r.totalLoads += b.nLoads;
        r.usedArea   += b.width * b.depth;
    }
    r.warehouseArea      = warehouseArea;
    r.percentageAreaUsed = (warehouseArea > 0.0)
                         ? std::min(1.0, r.usedArea / warehouseArea)
                         : 0.0;
    r.Q = computeQ(r.totalPrice, r.totalLoads, r.percentageAreaUsed);
}

// ----------------------------------------------------------------------------
// Feasibility check for a candidate footprint.
// ----------------------------------------------------------------------------
bool Optimizer::fits(const Rect& footprint, double bayHeight,
                     const std::vector<Bay>& placed,
                     const std::vector<Rect>& gapZones) const
{
    const double eps = opts_.epsilon;

    // Must lie inside the warehouse polygon.
    if (!data_.warehouse.containsRect(footprint, eps)) return false;

    // Ceiling clearance across the bay's X span.
    const double minH = data_.ceiling.minHeightInRange(footprint.minX(), footprint.maxX());
    if (bayHeight > minH + eps) return false;

    // Obstacle clearance.
    for (const auto& o : data_.obstacles) {
        if (footprint.overlaps(o.rect)) return false;
    }

    // Other-bay clearance (edge touch allowed by Rect::overlaps).
    for (const auto& b : placed) {
        Rect placedFootprint{ b.x, b.y, b.width, b.depth };
        if (footprint.overlaps(placedFootprint)) return false;
    }

    // Service gap zones must not overlap with the new footprint either.
    for (const auto& gz : gapZones) {
        if (footprint.overlaps(gz)) return false;
    }
    return true;
}

// ----------------------------------------------------------------------------
// Build the rotated footprint of a BayType.
// ----------------------------------------------------------------------------
struct OrientedBay { double width; double depth; int rotation; };

static std::vector<OrientedBay> orientationsFor(const BayType& t) {
    std::vector<OrientedBay> out;
    out.push_back({ t.width, t.depth, 0 });
    if (std::fabs(t.width - t.depth) > 1e-9) {
        out.push_back({ t.depth, t.width, 90 });
    }
    return out;
}

// ----------------------------------------------------------------------------
// One greedy bottom-left fill pass for a given preference order over bay
// types. The order is consulted at every candidate corner; the first
// (type, rotation) that fits gets placed.
// ----------------------------------------------------------------------------
OptimizerResult Optimizer::runStrategy(const std::vector<int>& typeOrder) const {
    OptimizerResult res;
    const Rect bbox = data_.warehouse.boundingBox();
    const double warehouseArea = data_.warehouse.area();

    // ---- candidate position pool ------------------------------------------
    // Each candidate is a "bottom-left" corner where a bay might originate.
    // We start from the warehouse bbox corner plus the corners of obstacles
    // (their +X and +Y sides). Every successful placement contributes its
    // own +X and +Y corners.
    auto cmp = [](const Point2& a, const Point2& b) {
        if (a.y != b.y) return a.y < b.y;
        return a.x < b.x;
    };
    std::set<std::pair<double, double>> seen;
    std::vector<Point2> candidates;
    auto addCandidate = [&](double x, double y) {
        // Snap-to-grid using a coarse epsilon to dedupe near-duplicates.
        auto k = std::make_pair(std::round(x * 1e6) / 1e6,
                                std::round(y * 1e6) / 1e6);
        if (seen.insert(k).second) candidates.push_back({x, y});
    };

    addCandidate(bbox.minX(), bbox.minY());
    // Concave corners of the polygon are good seed points too.
    for (const auto& v : data_.warehouse.vertices) addCandidate(v.x, v.y);
    // Obstacle corners (NE corner = right-of-the-obstacle, NW corner = above).
    for (const auto& o : data_.obstacles) {
        addCandidate(o.rect.maxX(), o.rect.minY());
        addCandidate(o.rect.minX(), o.rect.maxY());
        addCandidate(o.rect.maxX(), o.rect.maxY());
    }
    // Optional grid seeding.
    if (opts_.gridStep > 0.0) {
        for (double y = bbox.minY(); y <= bbox.maxY(); y += opts_.gridStep) {
            for (double x = bbox.minX(); x <= bbox.maxX(); x += opts_.gridStep) {
                addCandidate(x, y);
            }
        }
    }

    std::vector<Rect> gapZones;        // service-gap reservations
    std::vector<Bay>  placed;
    placed.reserve(256);

    // We loop the candidate list, popping the bottom-leftmost candidate, and
    // try to place. New candidates are pushed as bays land.
    while (!candidates.empty() && placed.size() < opts_.maxBays) {
        std::sort(candidates.begin(), candidates.end(), cmp);
        Point2 anchor = candidates.front();
        candidates.erase(candidates.begin());

        bool placedSomething = false;
        for (int idx : typeOrder) {
            const BayType& t = data_.bayTypes[idx];
            for (const auto& orient : orientationsFor(t)) {
                Rect footprint{ anchor.x, anchor.y, orient.width, orient.depth };
                // The gap is reserved on the +Y side (depth direction). It's a
                // ribbon of (width × gap) immediately above the bay.
                Rect gap{ anchor.x, anchor.y + orient.depth, orient.width, t.gap };

                if (!fits(footprint, t.height, placed, gapZones)) continue;

                // The gap ribbon is allowed to extend outside the warehouse:
                // it represents *clearance*, not occupied space, so we only
                // require it not to overlap obstacles or other bays. We do
                // however clip the part inside the warehouse for collision.
                bool gapOK = true;
                for (const auto& o : data_.obstacles) {
                    if (gap.overlaps(o.rect)) { gapOK = false; break; }
                }
                if (gapOK) {
                    for (const auto& b : placed) {
                        Rect bf{ b.x, b.y, b.width, b.depth };
                        if (gap.overlaps(bf)) { gapOK = false; break; }
                    }
                }
                if (!gapOK) continue;

                // ---- commit -----------------------------------------------
                Bay b;
                b.typeId      = t.id;
                b.x           = anchor.x;
                b.y           = anchor.y;
                b.rotationDeg = orient.rotation;
                b.width       = orient.width;
                b.depth       = orient.depth;
                b.height      = t.height;
                b.nLoads      = t.nLoads;
                b.price       = t.price;
                placed.push_back(b);
                if (t.gap > 0.0) gapZones.push_back(gap);

                // Spawn new candidate corners.
                addCandidate(b.x + b.width, b.y);
                addCandidate(b.x, b.y + b.depth + t.gap);
                addCandidate(b.x + b.width, b.y + b.depth + t.gap);

                placedSomething = true;
                break;
            }
            if (placedSomething) break;
        }
        // If nothing fit at this anchor we just drop it and keep going.
        (void)placedSomething;
    }

    res.bays = std::move(placed);
    recomputeMetrics(res, warehouseArea);
    return res;
}

// ----------------------------------------------------------------------------
// Top-level solve: try several orderings of bay types and keep the best.
// ----------------------------------------------------------------------------
OptimizerResult Optimizer::solve() {
    if (data_.bayTypes.empty()) {
        OptimizerResult empty;
        empty.warehouseArea = data_.warehouse.area();
        return empty;
    }

    const std::size_t n = data_.bayTypes.size();
    std::vector<int> baseOrder(n);
    std::iota(baseOrder.begin(), baseOrder.end(), 0);

    auto sortedBy = [&](auto keyFn) {
        std::vector<int> ord = baseOrder;
        std::sort(ord.begin(), ord.end(), [&](int a, int b) {
            return keyFn(data_.bayTypes[a]) < keyFn(data_.bayTypes[b]);
        });
        return ord;
    };

    std::vector<std::vector<int>> strategies;
    // Cheapest cost per load first.
    strategies.push_back(sortedBy([](const BayType& t){
        return t.price / std::max(1, t.nLoads);
    }));
    // Largest footprint first (fills area).
    strategies.push_back(sortedBy([](const BayType& t){
        return -(t.width * t.depth);
    }));
    // Highest load capacity first.
    strategies.push_back(sortedBy([](const BayType& t){ return -t.nLoads; }));
    // Cheapest first.
    strategies.push_back(sortedBy([](const BayType& t){ return t.price; }));
    // Catalogue order.
    strategies.push_back(baseOrder);
    // Best loads-per-area density first.
    strategies.push_back(sortedBy([](const BayType& t){
        const double a = std::max(1e-9, t.width * t.depth);
        return -(t.nLoads / a);
    }));

    OptimizerResult best;
    best.warehouseArea = data_.warehouse.area();
    best.Q = std::numeric_limits<double>::infinity();

    for (const auto& strat : strategies) {
        auto cand = runStrategy(strat);
        if (cand.bays.empty()) continue;
        if (cand.Q < best.Q) best = std::move(cand);
    }
    if (best.bays.empty()) {
        // No bay fit anywhere – return a sane empty result.
        best = OptimizerResult{};
        best.warehouseArea = data_.warehouse.area();
    }
    return best;
}

} // namespace whopt

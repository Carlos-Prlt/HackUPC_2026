// =============================================================================
//  Optimizer.hpp
//  Processing module: the layout solver.
//
//  Goal: place bays so as to minimize
//
//      Q = ((Σ price) / (Σ nLoads))^(2 - PercentageAreaUsed)
//
//  where PercentageAreaUsed = Σ(bay footprint area) / warehouse area, in [0, 1].
//
//  The solver is a multi-strategy greedy bottom-left-fill heuristic:
//
//    1. Try several orderings of bay types (cheapest-per-load first, biggest
//       footprint first, biggest load first, raw catalogue order, ...).
//    2. For each ordering, perform a corner-based bottom-left fill, attempting
//       both 0° and 90° rotations at each candidate corner.
//    3. Keep the placement set with the lowest Q.
//
//  Constraints honored at every placement:
//    - footprint stays inside the warehouse polygon
//    - footprint does not intersect any obstacle (edge-touch is allowed)
//    - footprint does not intersect any already-placed bay (edge-touch is allowed)
//    - bay height ≤ minimum ceiling height across the bay's X range
//    - the configured service `gap` is reserved on the +Y side of the bay
//      (a bay may still touch the warehouse edge / another bay, but two
//       gap zones may not overlap a footprint).
// =============================================================================
#pragma once

#include "core/Domain.hpp"
#include <vector>
#include <cstdint>

namespace whopt {

struct OptimizerResult {
    std::vector<Bay> bays;
    double totalPrice          = 0.0;
    long long totalLoads       = 0;
    double usedArea            = 0.0;
    double warehouseArea       = 0.0;
    double percentageAreaUsed  = 0.0;   // [0, 1]
    double Q                   = 0.0;   // the quality metric (lower is better)
};

class Optimizer {
public:
    struct Options {
        // Resolution of the candidate-position grid. Set to 0 (default) to use
        // corner-based candidate generation only — much faster and usually as
        // good. Set > 0 to additionally seed candidates on a grid.
        double  gridStep      = 0.0;
        // Numeric tolerance for "edge touching" comparisons.
        double  epsilon       = 1e-6;
        // Sets a hard cap on placed bays as a guardrail.
        std::size_t maxBays   = 100000;
    };

    explicit Optimizer(const Dataset& data) : data_(data), opts_() {}
    Optimizer(const Dataset& data, Options opts) : data_(data), opts_(opts) {}

    OptimizerResult solve();

private:
    const Dataset& data_;
    Options        opts_;

    // Internal helpers -------------------------------------------------------
    bool fits(const Rect& footprint, double bayHeight,
              const std::vector<Bay>& placed,
              const std::vector<Rect>& gapZones) const;

    OptimizerResult runStrategy(const std::vector<int>& typeOrder) const;

    static void recomputeMetrics(OptimizerResult& r, double warehouseArea);
};

} // namespace whopt

// =============================================================================
//  Scene.cpp
// =============================================================================
#include "output/Scene.hpp"

#include <cmath>
#include <functional>

namespace whopt {

Color SceneBuilder::colorForType(const std::string& id) {
    // Deterministic pastel-ish color from a hash of the id.
    std::hash<std::string> H;
    const std::size_t h = H(id);
    auto channel = [&](int shift) {
        const float v = static_cast<float>((h >> shift) & 0xFF) / 255.0f;
        return 0.45f + 0.45f * v;       // bias toward brighter mid-tones
    };
    return Color{ channel(0), channel(8), channel(16), 1.0f };
}

Scene SceneBuilder::build(const Dataset& data, const OptimizerResult& result) {
    Scene s;

    const Rect bbox = data.warehouse.boundingBox();
    s.floorMinX = static_cast<float>(bbox.minX());
    s.floorMinY = static_cast<float>(bbox.minY());
    s.floorMaxX = static_cast<float>(bbox.maxX());
    s.floorMaxY = static_cast<float>(bbox.maxY());
    s.ceilingMaxHeight = static_cast<float>(std::max(1.0, data.ceiling.maxHeight()));

    // ---- Warehouse outline ------------------------------------------------
    s.warehouseOutline.points.reserve(data.warehouse.vertices.size() + 1);
    for (const auto& v : data.warehouse.vertices) {
        s.warehouseOutline.points.push_back({
            static_cast<float>(v.x),
            static_cast<float>(v.y),
            0.0f
        });
    }
    if (!s.warehouseOutline.points.empty()) {
        s.warehouseOutline.points.push_back(s.warehouseOutline.points.front());
    }

    // ---- Obstacles (red, full-height to ceiling at obstacle's X center) ---
    for (const auto& o : data.obstacles) {
        const double cx = (o.rect.minX() + o.rect.maxX()) * 0.5;
        const double height = data.ceiling.minHeightInRange(cx, cx);
        Parallelepiped p;
        p.ox = static_cast<float>(o.rect.minX());
        p.oy = static_cast<float>(o.rect.minY());
        p.oz = 0.0f;
        p.sx = static_cast<float>(o.rect.w);
        p.sy = static_cast<float>(o.rect.d);
        p.sz = static_cast<float>(std::max(0.5, height));
        p.fill = Color{ 0.78f, 0.22f, 0.18f, 0.78f };
        p.edge = Color{ 0.45f, 0.05f, 0.05f, 1.0f };
        s.obstacles.push_back(p);
    }

    // ---- Ceiling panels (translucent, one per profile segment) ------------
    if (!data.ceiling.samples.empty()) {
        for (std::size_t i = 0; i < data.ceiling.samples.size(); ++i) {
            const double x0 = data.ceiling.samples[i].x;
            const double x1 = (i + 1 < data.ceiling.samples.size())
                              ? data.ceiling.samples[i + 1].x
                              : bbox.maxX();
            const double xa = std::max(x0, bbox.minX());
            const double xb = std::min(x1, bbox.maxX());
            if (xb <= xa) continue;
            Parallelepiped p;
            p.ox = static_cast<float>(xa);
            p.oy = static_cast<float>(bbox.minY());
            p.oz = static_cast<float>(data.ceiling.samples[i].height);
            p.sx = static_cast<float>(xb - xa);
            p.sy = static_cast<float>(bbox.d);
            p.sz = 0.05f * static_cast<float>(std::max(1.0, bbox.d)) / 50.0f;
            // We want a clearly visible but very thin panel; clamp:
            p.sz = std::max(0.05f, std::min(p.sz, 0.5f));
            p.fill = Color{ 0.55f, 0.65f, 0.85f, 0.18f };
            p.edge = Color{ 0.30f, 0.40f, 0.60f, 0.55f };
            s.ceilingPanels.push_back(p);
        }
    }

    // ---- Placed bays ------------------------------------------------------
    for (const auto& b : result.bays) {
        RenderableBay rb;
        rb.typeId      = b.typeId;
        rb.rotationDeg = b.rotationDeg;
        rb.box.ox = static_cast<float>(b.x);
        rb.box.oy = static_cast<float>(b.y);
        rb.box.oz = 0.0f;
        rb.box.sx = static_cast<float>(b.width);
        rb.box.sy = static_cast<float>(b.depth);
        rb.box.sz = static_cast<float>(b.height);
        rb.box.fill = colorForType(b.typeId);
        rb.box.edge = Color{ 0.05f, 0.05f, 0.10f, 1.0f };
        s.bays.push_back(std::move(rb));
    }

    s.Q                  = result.Q;
    s.totalPrice         = result.totalPrice;
    s.totalLoads         = result.totalLoads;
    s.percentageAreaUsed = result.percentageAreaUsed;
    return s;
}

} // namespace whopt

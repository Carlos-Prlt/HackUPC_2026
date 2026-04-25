// =============================================================================
//  Scene.hpp
//  Data Output module: converts the raw optimizer result (ID, X, Y, Rotation)
//  into structured C++ classes ready to be consumed by the OpenGL renderer.
//
//  The renderer treats every drawable thing as a parallelepiped (3D rectangular
//  prism) with a position, size, color and outline color. This is the only
//  shape the visualizer needs to know about.
// =============================================================================
#pragma once

#include "core/Domain.hpp"
#include "processing/Optimizer.hpp"

#include <array>
#include <string>
#include <vector>

namespace whopt {

struct Color {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

// A drawable axis-aligned box. The "origin" is the minimum corner.
struct Parallelepiped {
    float ox = 0.0f, oy = 0.0f, oz = 0.0f;     // origin (min corner)
    float sx = 0.0f, sy = 0.0f, sz = 0.0f;     // sizes along X, Y, Z
    Color fill;
    Color edge { 0.0f, 0.0f, 0.0f, 1.0f };
    bool  drawFaces = true;
    bool  drawEdges = true;
};

// One placed bay, with metadata kept around for HUD / picking.
struct RenderableBay {
    std::string    typeId;
    int            rotationDeg = 0;
    Parallelepiped box;
};

// Floor outline polyline (one closed loop of points). Used to draw the
// warehouse perimeter at z = 0.
struct PolylineLoop {
    std::vector<std::array<float, 3>> points;
    Color color { 0.15f, 0.18f, 0.22f, 1.0f };
};

struct Scene {
    // Coordinate system note: input data is X-Y on the floor with heights along
    // a third axis; in OpenGL we map to (X, Y_floor, Z=height). To make orbit
    // controls feel natural we'll lift heights along +Y in the renderer.
    // The Scene stores raw (X, Y_floor, Height) tuples; the renderer converts.

    float floorMinX = 0.f, floorMinY = 0.f;
    float floorMaxX = 0.f, floorMaxY = 0.f;
    float ceilingMaxHeight = 0.f;

    PolylineLoop                warehouseOutline;
    std::vector<Parallelepiped> obstacles;    // red boxes pinned to floor
    std::vector<Parallelepiped> ceilingPanels;// translucent ceiling profile
    std::vector<RenderableBay>  bays;

    // Aggregate metrics surfaced by the optimizer.
    double Q                  = 0.0;
    double totalPrice         = 0.0;
    long long totalLoads      = 0;
    double percentageAreaUsed = 0.0;
};

// Builds a Scene from the parsed dataset and the optimizer's result.
class SceneBuilder {
public:
    static Scene build(const Dataset& data, const OptimizerResult& result);

private:
    // Generate a stable color per bay typeId so multiple bays of the same
    // type look identical.
    static Color colorForType(const std::string& id);
};

} // namespace whopt

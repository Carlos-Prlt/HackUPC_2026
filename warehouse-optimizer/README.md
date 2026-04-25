# Warehouse Bay Placement Optimizer

A complete C++17 system that ingests warehouse, obstacle, ceiling-profile and bay-catalogue
data, computes a cost-optimal placement of storage bays, and renders the result in 3D using
OpenGL — both natively (via GLFW + GLEW) and on the web (via Emscripten / WebGL 2).

The project minimizes the objective

```
Q = ((Σ price) / (Σ loads)) ^ (2 - PercentageAreaUsed)
```

while respecting the warehouse perimeter (point-in-polygon), all obstacles, ceiling
clearance per bay-type height, per-bay service gaps, and bay-bay non-overlap (with shared
boundaries allowed).

## Project Layout (the four required modules)

| Module | Files | Purpose |
| ------ | ----- | ------- |
| **2.1 Data Input** | `include/input/DataLoader.hpp`, `src/input/DataLoader.cpp` | CSV parser and loaders for `WAREHOUSE.CSV`, `OBSTACLES.CSV`, `CEILING.CSV`, `TYPES_OF_BAYS.CSV`. Auto-detects header rows, supports `,` / `;` separators and EU decimal commas. |
| **2.2 Processing** | `include/processing/Optimizer.hpp`, `src/processing/Optimizer.cpp` | Greedy bottom-left placement with 6 selection strategies (cheapest/load, biggest, most loads, cheapest, catalogue order, density). Tries 0° / 90° rotations. Picks the strategy with the lowest `Q`. Prints `ID, X, Y, Rotation` per placed bay to **stdout**. |
| **2.3 Data Output** | `include/output/Scene.hpp`, `src/output/Scene.cpp` | Builds renderer-ready C++ classes (`Parallelepiped`, `RenderableBay`, `PolylineLoop`, `Scene`) from the optimizer's results. Deterministic per-type colours; ceiling segments and obstacles included. |
| **2.4 Visualization** | `include/visualization/*.hpp`, `src/visualization/*.cpp` | OpenGL renderer (GLSL ES 3.00) using parallelepipeds. Orbit camera with mouse. Compatible with Emscripten/WebGL 2. |

Shared domain types live in `include/core/Domain.hpp` and `src/core/Domain.cpp`
(point-in-polygon, rectangle tests, ceiling-height query, etc.).

## CSV Formats

All files live next to each other in a single data directory.

`WAREHOUSE.CSV` — vertices of the warehouse polygon (closed automatically):
```
CoordX,CoordY
0,0
50000,0
50000,30000
0,30000
```

`OBSTACLES.CSV` — axis-aligned rectangles `(x, y)` is the bottom-left corner:
```
CoordX,CoordY,Width,Depth
8000,12000,500,500
0,28000,15000,2000
```

`CEILING.CSV` — piecewise-constant height profile along X (right-continuous):
```
CoordX,Height
0,8000
20000,12000
35000,7500
```

`TYPES_OF_BAYS.CSV` — bay catalogue:
```
Id,Width,Depth,Height,Gap,nLoads,Price
SR-1100,2700,1100,7500,1200,18,1450
DR-2400,2700,2400,11000,1200,40,2900
COMPACT,1800, 800,5000,1000, 8, 520
```

All distances are in millimetres in the sample data, but the code is unit-agnostic — any
self-consistent unit works.

## Native Build (Linux / macOS)

Requirements: a C++17 compiler, CMake ≥ 3.16, **GLFW 3** and **GLEW** development packages.

On Debian / Ubuntu:
```bash
sudo apt install cmake build-essential libglfw3-dev libgl1-mesa-dev libglew-dev
```

Configure & build:
```bash
cd warehouse-optimizer
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Run with the bundled sample data:
```bash
./build/warehouse_optimizer data
```

A window opens and shows the 3D scene. The terminal prints
`ID, X, Y, Rotation` for every placed bay followed by a summary block on stderr.

Headless (no window, just terminal output):
```bash
./build/warehouse_optimizer --no-window data
```

### Controls

| Action | Mouse / Key |
| ------ | ----------- |
| Orbit (yaw / pitch) | Left-button drag |
| Pan | Right-button drag |
| Zoom | Scroll wheel |
| Quit | `Esc` |

## Web Build (Emscripten / WebAssembly + WebGL 2)

Requirements: the [Emscripten SDK](https://emscripten.org/) installed and activated
(`source emsdk_env.sh`).

Configure & build:
```bash
emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=Release
cmake --build build-web -j
```

This produces:
```
build-web/warehouse_optimizer.html
build-web/warehouse_optimizer.js
build-web/warehouse_optimizer.wasm
build-web/warehouse_optimizer.data    # bundled CSV data (preloaded)
```

Serve the directory over HTTP (browsers refuse to load `.wasm` from `file://`):
```bash
cd build-web
python3 -m http.server 8000
# then open http://localhost:8000/warehouse_optimizer.html
```

The shell page (`web/shell.html`) shows a canvas plus a console panel that mirrors stdout
so the `ID, X, Y, Rotation` listing is visible in-browser.

Notes:
- Shaders are written in **GLSL ES 3.00** so the same source compiles to OpenGL 3.3 Core
  on desktop and WebGL 2 in the browser.
- The CMake script switches automatically when `EMSCRIPTEN` is defined: links
  `-sUSE_GLFW=3 -sFULL_ES3=1 -sMIN_WEBGL_VERSION=2`, embeds the `data/` directory via
  `--preload-file`, and uses `web/shell.html` as the HTML template.
- The main loop is driven by `emscripten_set_main_loop` on the web target and by the
  GLFW event loop natively.

## Repository Layout

```
warehouse-optimizer/
├── CMakeLists.txt
├── README.md
├── data/                       sample CSV files
│   ├── WAREHOUSE.CSV
│   ├── OBSTACLES.CSV
│   ├── CEILING.CSV
│   └── TYPES_OF_BAYS.CSV
├── include/
│   ├── core/Domain.hpp
│   ├── input/DataLoader.hpp
│   ├── processing/Optimizer.hpp
│   ├── output/Scene.hpp
│   └── visualization/
│       ├── Application.hpp
│       ├── Camera.hpp
│       ├── GLPlatform.hpp
│       ├── Math.hpp
│       └── Shader.hpp
├── src/
│   ├── main.cpp
│   ├── core/Domain.cpp
│   ├── input/DataLoader.cpp
│   ├── processing/Optimizer.cpp
│   ├── output/Scene.cpp
│   └── visualization/
│       ├── Application.cpp
│       └── Shader.cpp
└── web/
    └── shell.html              Emscripten HTML template
```

## Algorithm Summary

The optimizer performs a *multi-strategy greedy bottom-left fill*:

1. Generate candidate corners from the warehouse polygon, obstacle NE/NW/SE corners, and
   the +X / +Y corners produced by every already-placed bay (and its service-gap zone).
2. For each candidate corner, try every (bay-type, rotation) pair sorted by the active
   strategy's preference and pick the first that fits. A fit requires:
   - the bay's footprint lies entirely inside the warehouse polygon,
   - it does not overlap any obstacle, any previously placed bay, or any reserved
     service-gap zone (the `Gap` strip on the +Y side of each bay),
   - the ceiling clearance over the bay's footprint is ≥ the bay's height (queried via
     the right-continuous piecewise-constant ceiling profile).
3. Re-run with each of the 6 strategies; the run with the lowest `Q` wins.
4. Stream the chosen placement to stdout in the required `ID, X, Y, Rotation` format.

`Q` is set to `+∞` whenever no bays are placed or area used is zero, so any feasible
layout strictly dominates the empty one.

## Verifying with the Sample Data

```bash
./build/warehouse_optimizer --no-window data | head
# ID, X, Y, Rotation
# SR-1100, 0, 0, 0
# SR-1100, 2700, 0, 0
# ...
```

A typical run on the bundled data places ~210 bays, uses ~50 % of the warehouse area, and
keeps every tall (`DR-2400`, h = 11000) bay out of the right-hand low-ceiling zone
(x ≥ 35000, ceiling = 7500).

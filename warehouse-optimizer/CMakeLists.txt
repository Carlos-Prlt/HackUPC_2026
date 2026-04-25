# =============================================================================
#  CMakeLists.txt — Warehouse Optimizer
#
#  Native build (Linux / macOS / Windows):
#     cmake -S . -B build
#     cmake --build build -j
#     ./build/warehouse_optimizer data
#
#  WebAssembly build (requires Emscripten activated):
#     emcmake cmake -S . -B build-web
#     cmake --build build-web -j
#     # Output: build-web/warehouse_optimizer.{js,wasm,html}
#     # Serve via:  python3 -m http.server  (from build-web/)
#
#  Native dependencies (Linux):
#     sudo apt install libglfw3-dev libgl1-mesa-dev
# =============================================================================
cmake_minimum_required(VERSION 3.16)
project(warehouse_optimizer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

set(WHOPT_SOURCES
    src/main.cpp
    src/core/Domain.cpp
    src/input/DataLoader.cpp
    src/processing/Optimizer.cpp
    src/output/Scene.cpp
    src/visualization/Shader.cpp
    src/visualization/Application.cpp
)

add_executable(warehouse_optimizer ${WHOPT_SOURCES})
target_include_directories(warehouse_optimizer PRIVATE include)

# ---------------------------------------------------------------------------
# Emscripten / WebAssembly path
# ---------------------------------------------------------------------------
if(EMSCRIPTEN)
    message(STATUS "Building for Emscripten / WebAssembly")
    set(CMAKE_EXECUTABLE_SUFFIX ".html")

    # Use Emscripten-bundled GLFW3, full GLES3 / WebGL2, embed the data dir.
    set(EM_LINK_FLAGS
        "-sUSE_GLFW=3"
        "-sFULL_ES3=1"
        "-sMIN_WEBGL_VERSION=2"
        "-sMAX_WEBGL_VERSION=2"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sNO_EXIT_RUNTIME=1"
        "-sASYNCIFY=0"
        "--shell-file=${CMAKE_SOURCE_DIR}/web/shell.html"
        "--preload-file=${CMAKE_SOURCE_DIR}/data@/data"
    )
    string(JOIN " " EM_LINK_FLAGS_STR ${EM_LINK_FLAGS})
    set_target_properties(warehouse_optimizer PROPERTIES
        LINK_FLAGS "${EM_LINK_FLAGS_STR}"
    )

# ---------------------------------------------------------------------------
# Native path
# ---------------------------------------------------------------------------
else()
    find_package(OpenGL REQUIRED)
    find_package(glfw3 QUIET)
    if(NOT glfw3_FOUND)
        # Fallback search via pkg-config or hard-coded common paths.
        find_path(GLFW_INCLUDE_DIR GLFW/glfw3.h
            HINTS /usr/include /usr/local/include /opt/homebrew/include)
        find_library(GLFW_LIBRARY NAMES glfw glfw3
            HINTS /usr/lib /usr/local/lib /opt/homebrew/lib)
        if(NOT GLFW_INCLUDE_DIR OR NOT GLFW_LIBRARY)
            message(FATAL_ERROR
                "GLFW3 not found. On Debian/Ubuntu: sudo apt install libglfw3-dev. "
                "On macOS: brew install glfw. On Windows use vcpkg or set CMAKE_PREFIX_PATH.")
        endif()
        target_include_directories(warehouse_optimizer PRIVATE ${GLFW_INCLUDE_DIR})
        target_link_libraries(warehouse_optimizer PRIVATE ${GLFW_LIBRARY})
    else()
        target_link_libraries(warehouse_optimizer PRIVATE glfw)
    endif()

    target_link_libraries(warehouse_optimizer PRIVATE OpenGL::GL)

    # Native desktop builds need a GL function loader (the system GL header
    # only exposes the 1.x fixed-function API). We use GLEW because it is
    # universally packaged. Emscripten doesn't need this — its GLES3 entry
    # points are linked statically by the compiler driver.
    find_package(GLEW QUIET)
    if(GLEW_FOUND)
        target_link_libraries(warehouse_optimizer PRIVATE GLEW::GLEW)
        target_compile_definitions(warehouse_optimizer PRIVATE WHOPT_USE_GLEW=1)
    else()
        find_path(GLEW_INCLUDE_DIR GL/glew.h
            HINTS /usr/include /usr/local/include /opt/homebrew/include)
        find_library(GLEW_LIBRARY NAMES GLEW glew32 glew
            HINTS /usr/lib /usr/local/lib /opt/homebrew/lib /usr/lib/x86_64-linux-gnu)
        if(NOT GLEW_INCLUDE_DIR OR NOT GLEW_LIBRARY)
            message(FATAL_ERROR
                "GLEW not found. On Debian/Ubuntu: sudo apt install libglew-dev. "
                "On macOS: brew install glew. On Windows use vcpkg.")
        endif()
        target_include_directories(warehouse_optimizer PRIVATE ${GLEW_INCLUDE_DIR})
        target_link_libraries(warehouse_optimizer PRIVATE ${GLEW_LIBRARY})
        target_compile_definitions(warehouse_optimizer PRIVATE WHOPT_USE_GLEW=1)
    endif()

    # On Linux GLFW also needs X11/threading libs. Pull them in defensively.
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        find_package(Threads REQUIRED)
        target_link_libraries(warehouse_optimizer PRIVATE
            Threads::Threads ${CMAKE_DL_LIBS})
    endif()
endif()

# ---------------------------------------------------------------------------
# Copy data files next to the executable for ergonomic native runs
# ---------------------------------------------------------------------------
if(NOT EMSCRIPTEN)
    add_custom_command(TARGET warehouse_optimizer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/data
            $<TARGET_FILE_DIR:warehouse_optimizer>/data)
endif()

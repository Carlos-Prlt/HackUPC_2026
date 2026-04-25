// =============================================================================
//  Application.hpp
//  GLFW-hosted OpenGL application that ingests a Scene from the output module
//  and renders the warehouse, obstacles, ceiling and bays as parallelepipeds.
//
//  This single class works for both desktop and Emscripten/WebGL2 builds. The
//  main loop is exposed as `tick()` and is driven either by glfwWindowShouldLoop
//  on desktop or by emscripten_set_main_loop in the browser build (see main.cpp).
// =============================================================================
#pragma once

#include "visualization/GLPlatform.hpp"
#include "visualization/Shader.hpp"
#include "visualization/Camera.hpp"
#include "output/Scene.hpp"

namespace whopt {

class Application {
public:
    struct Config {
        int  width  = 1280;
        int  height = 720;
        const char* title = "Warehouse Optimizer";
    };

    bool init(const Config& cfg, Scene scene);
    void shutdown();

    // Renders one frame and processes events. Returns false when the user
    // closed the window (desktop only; on web we never return false).
    bool tick();

    // Main loop helpers.
    void runDesktop();             // blocks until the window is closed
    static void emTickStatic(void* self) { static_cast<Application*>(self)->tick(); }

    GLFWwindow* window() const { return window_; }
    const Scene& scene() const { return scene_; }

private:
    void initBuffers();
    void buildScene();
    void drawBox(const Parallelepiped& box, const Mat4& vp) const;
    void drawOutline(const PolylineLoop& loop, const Mat4& vp) const;
    void handleInput(double dt);

    // GLFW input bridges
    static void cbResize(GLFWwindow*, int w, int h);
    static void cbMouseButton(GLFWwindow*, int button, int action, int mods);
    static void cbCursorPos(GLFWwindow*, double xpos, double ypos);
    static void cbScroll(GLFWwindow*, double xoff, double yoff);
    static void cbKey(GLFWwindow*, int key, int scancode, int action, int mods);

    // Members
    GLFWwindow* window_   = nullptr;
    int         winW_     = 1280;
    int         winH_     = 720;
    Scene       scene_;
    Camera      camera_;

    Shader      boxShader_;
    Shader      lineShader_;

    // Cube VAO/VBO and a unit-line VAO/VBO for outlines and floor grid.
    GLuint cubeVAO_ = 0, cubeVBO_ = 0, cubeEBO_ = 0;
    GLuint edgeVAO_ = 0, edgeVBO_ = 0;
    GLuint lineVAO_ = 0, lineVBO_ = 0;

    // Mouse state
    bool   leftDown_   = false;
    bool   rightDown_  = false;
    double lastMouseX_ = 0.0;
    double lastMouseY_ = 0.0;

    double lastTime_ = 0.0;
};

} // namespace whopt

// =============================================================================
//  Application.cpp
// =============================================================================
#include "visualization/Application.hpp"

#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>

namespace whopt {

// -----------------------------------------------------------------------------
// Embedded shaders. GLSL ES 300 — works on both WebGL2 and desktop GL 3.3+
// when contexts are created with the GLES profile / forward-compat flags.
// -----------------------------------------------------------------------------
static const char* kBoxVS = R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
uniform mat4 uModel;
uniform mat4 uViewProj;
out vec3 vNormal;
out vec3 vWorld;
void main() {
    vec4 wp = uModel * vec4(aPos, 1.0);
    vWorld = wp.xyz;
    vNormal = mat3(uModel) * aNormal;
    gl_Position = uViewProj * wp;
}
)";

static const char* kBoxFS = R"(#version 300 es
precision mediump float;
in vec3 vNormal;
in vec3 vWorld;
uniform vec4  uColor;
uniform vec3  uLightDir;
out vec4 outColor;
void main() {
    vec3 n = normalize(vNormal);
    float diff = clamp(dot(n, normalize(-uLightDir)), 0.0, 1.0);
    float ambient = 0.35;
    vec3 lit = uColor.rgb * (ambient + 0.65 * diff);
    outColor = vec4(lit, uColor.a);
}
)";

static const char* kLineVS = R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPos;
uniform mat4 uViewProj;
uniform mat4 uModel;
void main() {
    gl_Position = uViewProj * uModel * vec4(aPos, 1.0);
}
)";

static const char* kLineFS = R"(#version 300 es
precision mediump float;
uniform vec4 uColor;
out vec4 outColor;
void main() { outColor = uColor; }
)";

// -----------------------------------------------------------------------------
// Cube mesh: 6 faces × 2 triangles × 3 vertices, with per-face normals.
// Positions are in [0,1] so we can scale them by the parallelepiped size and
// translate by its origin.
// -----------------------------------------------------------------------------
namespace {
struct Vertex { float px, py, pz, nx, ny, nz; };

const std::vector<Vertex>& cubeVerts() {
    static const std::vector<Vertex> v = {
        // -X
        {0,0,0,-1,0,0},{0,0,1,-1,0,0},{0,1,1,-1,0,0},{0,0,0,-1,0,0},{0,1,1,-1,0,0},{0,1,0,-1,0,0},
        // +X
        {1,0,0, 1,0,0},{1,1,0, 1,0,0},{1,1,1, 1,0,0},{1,0,0, 1,0,0},{1,1,1, 1,0,0},{1,0,1, 1,0,0},
        // -Y
        {0,0,0, 0,-1,0},{1,0,0, 0,-1,0},{1,0,1, 0,-1,0},{0,0,0, 0,-1,0},{1,0,1, 0,-1,0},{0,0,1, 0,-1,0},
        // +Y
        {0,1,0, 0, 1,0},{0,1,1, 0, 1,0},{1,1,1, 0, 1,0},{0,1,0, 0, 1,0},{1,1,1, 0, 1,0},{1,1,0, 0, 1,0},
        // -Z
        {0,0,0, 0,0,-1},{0,1,0, 0,0,-1},{1,1,0, 0,0,-1},{0,0,0, 0,0,-1},{1,1,0, 0,0,-1},{1,0,0, 0,0,-1},
        // +Z
        {0,0,1, 0,0, 1},{1,0,1, 0,0, 1},{1,1,1, 0,0, 1},{0,0,1, 0,0, 1},{1,1,1, 0,0, 1},{0,1,1, 0,0, 1},
    };
    return v;
}

// 12 cube edges as line pairs in [0,1].
const std::vector<float>& cubeEdges() {
    static const std::vector<float> e = {
        0,0,0, 1,0,0,  1,0,0, 1,1,0,  1,1,0, 0,1,0,  0,1,0, 0,0,0,    // bottom
        0,0,1, 1,0,1,  1,0,1, 1,1,1,  1,1,1, 0,1,1,  0,1,1, 0,0,1,    // top
        0,0,0, 0,0,1,  1,0,0, 1,0,1,  1,1,0, 1,1,1,  0,1,0, 0,1,1     // verticals
    };
    return e;
}
} // anon

// -----------------------------------------------------------------------------
//  Init / shutdown
// -----------------------------------------------------------------------------
bool Application::init(const Config& cfg, Scene scene) {
    scene_ = std::move(scene);
    winW_  = cfg.width;
    winH_  = cfg.height;

    if (!glfwInit()) {
        std::cerr << "[Application] glfwInit failed\n";
        return false;
    }

#if defined(__EMSCRIPTEN__)
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#else
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    #ifdef __APPLE__
        glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GL_TRUE);
    #endif
#endif

    window_ = glfwCreateWindow(cfg.width, cfg.height, cfg.title, nullptr, nullptr);
    if (!window_) {
        std::cerr << "[Application] glfwCreateWindow failed\n";
        glfwTerminate();
        return false;
    }
    glfwMakeContextCurrent(window_);
    glfwSetWindowUserPointer(window_, this);
    glfwSetFramebufferSizeCallback(window_, cbResize);
    glfwSetMouseButtonCallback   (window_, cbMouseButton);
    glfwSetCursorPosCallback     (window_, cbCursorPos);
    glfwSetScrollCallback        (window_, cbScroll);
    glfwSetKeyCallback           (window_, cbKey);

#if !defined(__EMSCRIPTEN__) && defined(WHOPT_USE_GLAD)
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        std::cerr << "[Application] failed to load GL via GLAD\n";
        return false;
    }
#endif
#if !defined(__EMSCRIPTEN__) && defined(WHOPT_USE_GLEW)
    glewExperimental = GL_TRUE;     // required for core profiles
    GLenum glewErr = glewInit();
    if (glewErr != GLEW_OK) {
        std::cerr << "[Application] failed to load GL via GLEW: "
                  << glewGetErrorString(glewErr) << "\n";
        return false;
    }
    // GLEW likes to flag a benign GL_INVALID_ENUM after init on core profiles.
    while (glGetError() != GL_NO_ERROR) { /* drain */ }
#endif

    if (!boxShader_.buildFromSource(kBoxVS, kBoxFS))   return false;
    if (!lineShader_.buildFromSource(kLineVS, kLineFS)) return false;

    initBuffers();
    buildScene();

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0.94f, 0.95f, 0.97f, 1.0f);

    lastTime_ = glfwGetTime();
    return true;
}

void Application::initBuffers() {
    // Cube faces (positions + normals)
    const auto& cv = cubeVerts();
    glGenVertexArrays(1, &cubeVAO_);
    glGenBuffers(1, &cubeVBO_);
    glBindVertexArray(cubeVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, cubeVBO_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(cv.size() * sizeof(Vertex)),
                 cv.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, px)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, nx)));
    glBindVertexArray(0);

    // Cube edges (lines)
    const auto& ce = cubeEdges();
    glGenVertexArrays(1, &edgeVAO_);
    glGenBuffers(1, &edgeVBO_);
    glBindVertexArray(edgeVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, edgeVBO_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(ce.size() * sizeof(float)),
                 ce.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glBindVertexArray(0);

    // Generic line strip VBO (resized per draw)
    glGenVertexArrays(1, &lineVAO_);
    glGenBuffers(1, &lineVBO_);
    glBindVertexArray(lineVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, lineVBO_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glBindVertexArray(0);
}

void Application::buildScene() {
    // Frame the camera around the warehouse extents.
    const float cx = (scene_.floorMinX + scene_.floorMaxX) * 0.5f;
    const float cy = (scene_.floorMinY + scene_.floorMaxY) * 0.5f;
    const float w  = scene_.floorMaxX - scene_.floorMinX;
    const float d  = scene_.floorMaxY - scene_.floorMinY;
    const float h  = scene_.ceilingMaxHeight;
    const float diag = std::sqrt(w * w + d * d + h * h);

    // Coordinate mapping: input is X-right, Y-forward, height=Z. We render
    // with Y-up (Y = height), so we'll re-map at draw time. Camera target sits
    // at the centre of the floor in world space.
    camera_.setTarget(Vec3(cx, h * 0.25f, cy));
    camera_.setDistance(diag * 0.9f);
    camera_.setBounds(diag * 0.05f, diag * 5.0f);
}

void Application::shutdown() {
    if (cubeVBO_) { glDeleteBuffers(1, &cubeVBO_); cubeVBO_ = 0; }
    if (cubeVAO_) { glDeleteVertexArrays(1, &cubeVAO_); cubeVAO_ = 0; }
    if (edgeVBO_) { glDeleteBuffers(1, &edgeVBO_); edgeVBO_ = 0; }
    if (edgeVAO_) { glDeleteVertexArrays(1, &edgeVAO_); edgeVAO_ = 0; }
    if (lineVBO_) { glDeleteBuffers(1, &lineVBO_); lineVBO_ = 0; }
    if (lineVAO_) { glDeleteVertexArrays(1, &lineVAO_); lineVAO_ = 0; }
    if (window_)  { glfwDestroyWindow(window_); window_ = nullptr; }
    glfwTerminate();
}

// -----------------------------------------------------------------------------
//  Drawing
// -----------------------------------------------------------------------------
//
// Coordinate convention used inside the shader: world Y is up. Inputs use
// (X, Y_floor, Height); we map those onto (X, Height, Y_floor) at the model
// matrix level so that the floor sits in the X-Z plane.
//
// We bake that swap into the model matrix here.
//
static Mat4 footprintModel(float ox, float oy, float oz, float sx, float sy, float sz) {
    Mat4 T = Mat4::identity();
    T.m[12] = ox;
    T.m[13] = oz;       // height -> world Y
    T.m[14] = oy;       // floor Y -> world Z
    Mat4 S{};
    S.m[0]  = sx;
    S.m[5]  = sz;       // height extent -> world Y extent
    S.m[10] = sy;       // floor depth -> world Z extent
    S.m[15] = 1.0f;
    return T * S;
}

void Application::drawBox(const Parallelepiped& b, const Mat4& vp) const {
    Mat4 model = footprintModel(b.ox, b.oy, b.oz, b.sx, b.sy, b.sz);

    if (b.drawFaces) {
        boxShader_.use();
        boxShader_.setMat4("uViewProj", vp);
        boxShader_.setMat4("uModel",    model);
        boxShader_.setVec3("uLightDir", -0.45f, -0.85f, -0.30f);
        boxShader_.setVec4("uColor",    b.fill.r, b.fill.g, b.fill.b, b.fill.a);
        glBindVertexArray(cubeVAO_);
        glDrawArrays(GL_TRIANGLES, 0, 36);
        glBindVertexArray(0);
    }
    if (b.drawEdges) {
        lineShader_.use();
        lineShader_.setMat4("uViewProj", vp);
        lineShader_.setMat4("uModel",    model);
        lineShader_.setVec4("uColor", b.edge.r, b.edge.g, b.edge.b, b.edge.a);
        glBindVertexArray(edgeVAO_);
        glDrawArrays(GL_LINES, 0, 24);
        glBindVertexArray(0);
    }
}

void Application::drawOutline(const PolylineLoop& loop, const Mat4& vp) const {
    if (loop.points.size() < 2) return;
    std::vector<float> data;
    data.reserve(loop.points.size() * 3);
    for (const auto& p : loop.points) {
        // Same axis swap as boxes.
        data.push_back(p[0]);   // x
        data.push_back(p[2]);   // height (0 here)
        data.push_back(p[1]);   // y_floor
    }
    glBindVertexArray(lineVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, lineVBO_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(data.size() * sizeof(float)),
                 data.data(), GL_DYNAMIC_DRAW);
    lineShader_.use();
    lineShader_.setMat4("uViewProj", vp);
    lineShader_.setMat4("uModel",    Mat4::identity());
    lineShader_.setVec4("uColor", loop.color.r, loop.color.g, loop.color.b, loop.color.a);
    glDrawArrays(GL_LINE_STRIP, 0, static_cast<GLsizei>(loop.points.size()));
    glBindVertexArray(0);
}

bool Application::tick() {
    if (!window_) return false;

    glfwPollEvents();
    if (glfwWindowShouldClose(window_)) return false;

    const double now = glfwGetTime();
    const double dt  = now - lastTime_;
    lastTime_ = now;
    handleInput(dt);

    glViewport(0, 0, winW_, winH_);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const float aspect = (winH_ > 0) ? float(winW_) / float(winH_) : 1.0f;
    Mat4 vp = camera_.projection(aspect) * camera_.view();

    // 1. Floor: a thin parallelepiped just below z=0 to give depth cues.
    {
        Parallelepiped floor;
        floor.ox = scene_.floorMinX;
        floor.oy = scene_.floorMinY;
        floor.oz = -0.02f * std::max(scene_.ceilingMaxHeight, 1.0f);
        floor.sx = scene_.floorMaxX - scene_.floorMinX;
        floor.sy = scene_.floorMaxY - scene_.floorMinY;
        floor.sz = 0.02f * std::max(scene_.ceilingMaxHeight, 1.0f);
        floor.fill = Color{ 0.85f, 0.86f, 0.88f, 1.0f };
        floor.edge = Color{ 0.55f, 0.55f, 0.55f, 1.0f };
        floor.drawEdges = false;
        drawBox(floor, vp);
    }

    // 2. Warehouse outline.
    drawOutline(scene_.warehouseOutline, vp);

    // 3. Ceiling panels (translucent).
    glDepthMask(GL_FALSE);
    for (const auto& p : scene_.ceilingPanels) drawBox(p, vp);
    glDepthMask(GL_TRUE);

    // 4. Obstacles.
    for (const auto& p : scene_.obstacles) drawBox(p, vp);

    // 5. Bays.
    for (const auto& b : scene_.bays) drawBox(b.box, vp);

    glfwSwapBuffers(window_);
    return true;
}

void Application::runDesktop() {
#if !defined(__EMSCRIPTEN__)
    while (tick()) {}
#endif
}

// -----------------------------------------------------------------------------
//  Input
// -----------------------------------------------------------------------------
void Application::handleInput(double /*dt*/) {
    // Reserved for future use (keyboard pan, etc.). All major interactions
    // currently flow through GLFW callbacks below.
}

void Application::cbResize(GLFWwindow* w, int width, int height) {
    auto* self = static_cast<Application*>(glfwGetWindowUserPointer(w));
    if (!self) return;
    self->winW_ = width;
    self->winH_ = height;
}

void Application::cbMouseButton(GLFWwindow* w, int button, int action, int /*mods*/) {
    auto* self = static_cast<Application*>(glfwGetWindowUserPointer(w));
    if (!self) return;
    const bool down = (action == GLFW_PRESS);
    if (button == GLFW_MOUSE_BUTTON_LEFT)  self->leftDown_  = down;
    if (button == GLFW_MOUSE_BUTTON_RIGHT) self->rightDown_ = down;
    glfwGetCursorPos(w, &self->lastMouseX_, &self->lastMouseY_);
}

void Application::cbCursorPos(GLFWwindow* w, double xpos, double ypos) {
    auto* self = static_cast<Application*>(glfwGetWindowUserPointer(w));
    if (!self) return;
    const double dx = xpos - self->lastMouseX_;
    const double dy = ypos - self->lastMouseY_;
    self->lastMouseX_ = xpos;
    self->lastMouseY_ = ypos;
    if (self->leftDown_)  self->camera_.onMouseDrag(static_cast<float>(dx), static_cast<float>(dy));
    if (self->rightDown_) self->camera_.onPan      (static_cast<float>(dx), static_cast<float>(dy));
}

void Application::cbScroll(GLFWwindow* w, double /*xoff*/, double yoff) {
    auto* self = static_cast<Application*>(glfwGetWindowUserPointer(w));
    if (!self) return;
    self->camera_.onScroll(static_cast<float>(yoff));
}

void Application::cbKey(GLFWwindow* w, int key, int /*scancode*/, int action, int /*mods*/) {
    if (action != GLFW_PRESS) return;
    if (key == GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(w, GLFW_TRUE);
}

} // namespace whopt

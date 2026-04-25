// =============================================================================
//  GLPlatform.hpp
//  Single point of GL header inclusion. Lets the rest of the visualization
//  code be agnostic to whether we're building natively (desktop OpenGL via
//  GLAD/GLEW) or for WebAssembly/WebGL2 via Emscripten (GLES3).
//
//  We deliberately stick to the OpenGL ES 3.0 / GLSL ES 300 subset, which is
//  compatible with WebGL2 *and* with desktop OpenGL 4.1+ (with the ES profile
//  enabled). Shaders are written in `#version 300 es` form.
// =============================================================================
#pragma once

#if defined(__EMSCRIPTEN__)
    // Emscripten: WebGL2 maps onto GLES3 entry points.
    #include <GLES3/gl3.h>
    #include <emscripten/emscripten.h>
    #include <emscripten/html5.h>
    #define GLFW_INCLUDE_ES3
    #include <GLFW/glfw3.h>
#else
    // Desktop: prefer GLEW (most widely packaged), fall back to GLAD or the
    // raw system GL headers if neither loader is available.
    #if defined(WHOPT_USE_GLEW)
        #include <GL/glew.h>
    #elif defined(WHOPT_USE_GLAD)
        #include <glad/glad.h>
    #else
        #if defined(__APPLE__)
            #define GL_SILENCE_DEPRECATION
            #include <OpenGL/gl3.h>
        #else
            // Last-resort: rely on the legacy headers. Calls to 3.x entry
            // points won't resolve, so the build will fail at link time
            // unless WHOPT_USE_GLEW or WHOPT_USE_GLAD is defined.
            #include <GL/gl.h>
        #endif
    #endif
    #include <GLFW/glfw3.h>
#endif

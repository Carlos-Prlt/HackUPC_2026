// =============================================================================
//  Shader.hpp
//  Minimal GLSL program wrapper compatible with GLES3 / WebGL2.
// =============================================================================
#pragma once

#include "visualization/GLPlatform.hpp"
#include "visualization/Math.hpp"

#include <string>

namespace whopt {

class Shader {
public:
    Shader();
    ~Shader();
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    // Compile from source strings. Returns false (and writes to stderr) on
    // failure. The instance remains valid but inactive.
    bool buildFromSource(const std::string& vertSrc, const std::string& fragSrc);

    void use() const;
    GLuint id() const { return program_; }

    // Uniform setters by name (cached). Silently no-op if uniform missing.
    void setMat4 (const std::string& name, const Mat4& v) const;
    void setVec3 (const std::string& name, float x, float y, float z) const;
    void setVec4 (const std::string& name, float x, float y, float z, float w) const;
    void setFloat(const std::string& name, float v) const;

private:
    GLuint program_ = 0;
    GLint  loc(const std::string& name) const;
};

} // namespace whopt

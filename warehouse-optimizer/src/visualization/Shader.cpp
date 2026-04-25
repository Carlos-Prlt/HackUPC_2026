// =============================================================================
//  Shader.cpp
// =============================================================================
#include "visualization/Shader.hpp"

#include <iostream>
#include <unordered_map>
#include <vector>

namespace whopt {

namespace {
GLuint compileStage(GLenum stage, const std::string& src) {
    GLuint sh = glCreateShader(stage);
    const char* p = src.c_str();
    glShaderSource(sh, 1, &p, nullptr);
    glCompileShader(sh);
    GLint ok = GL_FALSE;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint logLen = 0;
        glGetShaderiv(sh, GL_INFO_LOG_LENGTH, &logLen);
        std::vector<char> log(static_cast<std::size_t>(logLen + 1), 0);
        glGetShaderInfoLog(sh, logLen, nullptr, log.data());
        std::cerr << "[Shader] " << (stage == GL_VERTEX_SHADER ? "vertex" : "fragment")
                  << " compile failed: " << log.data() << std::endl;
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}
} // namespace

Shader::Shader() = default;

Shader::~Shader() {
    if (program_) glDeleteProgram(program_);
}

bool Shader::buildFromSource(const std::string& vs, const std::string& fs) {
    GLuint v = compileStage(GL_VERTEX_SHADER,   vs);
    if (!v) return false;
    GLuint f = compileStage(GL_FRAGMENT_SHADER, fs);
    if (!f) { glDeleteShader(v); return false; }

    program_ = glCreateProgram();
    glAttachShader(program_, v);
    glAttachShader(program_, f);
    glLinkProgram(program_);
    glDeleteShader(v);
    glDeleteShader(f);

    GLint ok = GL_FALSE;
    glGetProgramiv(program_, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint logLen = 0;
        glGetProgramiv(program_, GL_INFO_LOG_LENGTH, &logLen);
        std::vector<char> log(static_cast<std::size_t>(logLen + 1), 0);
        glGetProgramInfoLog(program_, logLen, nullptr, log.data());
        std::cerr << "[Shader] link failed: " << log.data() << std::endl;
        glDeleteProgram(program_);
        program_ = 0;
        return false;
    }
    return true;
}

void Shader::use() const {
    if (program_) glUseProgram(program_);
}

GLint Shader::loc(const std::string& name) const {
    static thread_local std::unordered_map<GLuint, std::unordered_map<std::string, GLint>> cache;
    auto& byProg = cache[program_];
    auto it = byProg.find(name);
    if (it != byProg.end()) return it->second;
    GLint l = glGetUniformLocation(program_, name.c_str());
    byProg.emplace(name, l);
    return l;
}

void Shader::setMat4(const std::string& n, const Mat4& v) const {
    GLint l = loc(n);
    if (l >= 0) glUniformMatrix4fv(l, 1, GL_FALSE, v.m.data());
}
void Shader::setVec3(const std::string& n, float x, float y, float z) const {
    GLint l = loc(n);
    if (l >= 0) glUniform3f(l, x, y, z);
}
void Shader::setVec4(const std::string& n, float x, float y, float z, float w) const {
    GLint l = loc(n);
    if (l >= 0) glUniform4f(l, x, y, z, w);
}
void Shader::setFloat(const std::string& n, float v) const {
    GLint l = loc(n);
    if (l >= 0) glUniform1f(l, v);
}

} // namespace whopt

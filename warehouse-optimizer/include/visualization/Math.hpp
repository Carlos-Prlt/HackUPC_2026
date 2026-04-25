// =============================================================================
//  Math.hpp
//  Tiny header-only linalg helpers. Keeping them in-house lets us compile for
//  Emscripten without adding a GLM dependency.
// =============================================================================
#pragma once
#include <cmath>
#include <array>

namespace whopt {

struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3() = default;
    Vec3(float X, float Y, float Z) : x(X), y(Y), z(Z) {}
    Vec3 operator+(const Vec3& o) const { return {x+o.x, y+o.y, z+o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x-o.x, y-o.y, z-o.z}; }
    Vec3 operator*(float s)        const { return {x*s, y*s, z*s}; }
};
inline float dot(const Vec3& a, const Vec3& b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return { a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x };
}
inline float length(const Vec3& v) { return std::sqrt(dot(v, v)); }
inline Vec3 normalize(const Vec3& v) {
    float l = length(v);
    if (l < 1e-9f) return {0, 0, 0};
    return { v.x/l, v.y/l, v.z/l };
}

// Column-major 4x4 matrix; OpenGL convention.
struct Mat4 {
    std::array<float, 16> m{};

    static Mat4 identity() {
        Mat4 r{}; r.m[0]=r.m[5]=r.m[10]=r.m[15]=1.0f; return r;
    }
    static Mat4 translation(const Vec3& t) {
        Mat4 r = identity();
        r.m[12]=t.x; r.m[13]=t.y; r.m[14]=t.z;
        return r;
    }
    static Mat4 scale(const Vec3& s) {
        Mat4 r{};
        r.m[0]=s.x; r.m[5]=s.y; r.m[10]=s.z; r.m[15]=1.f;
        return r;
    }
    static Mat4 perspective(float fovYRad, float aspect, float zn, float zf) {
        const float f = 1.0f / std::tan(fovYRad * 0.5f);
        Mat4 r{};
        r.m[0] = f / aspect;
        r.m[5] = f;
        r.m[10]= (zf + zn) / (zn - zf);
        r.m[11]= -1.0f;
        r.m[14]= (2.f * zf * zn) / (zn - zf);
        return r;
    }
    static Mat4 lookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
        Vec3 f = normalize(target - eye);
        Vec3 s = normalize(cross(f, up));
        Vec3 u = cross(s, f);
        Mat4 r = identity();
        r.m[0] = s.x;  r.m[4] = s.y;  r.m[8]  = s.z;
        r.m[1] = u.x;  r.m[5] = u.y;  r.m[9]  = u.z;
        r.m[2] =-f.x;  r.m[6] =-f.y;  r.m[10] =-f.z;
        r.m[12]= -dot(s, eye);
        r.m[13]= -dot(u, eye);
        r.m[14]=  dot(f, eye);
        return r;
    }
    Mat4 operator*(const Mat4& o) const {
        Mat4 r{};
        for (int c = 0; c < 4; ++c)
        for (int rIx = 0; rIx < 4; ++rIx) {
            float v = 0;
            for (int k = 0; k < 4; ++k) {
                v += m[k * 4 + rIx] * o.m[c * 4 + k];
            }
            r.m[c * 4 + rIx] = v;
        }
        return r;
    }
};

} // namespace whopt

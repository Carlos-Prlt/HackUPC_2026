// =============================================================================
//  Camera.hpp
//  Simple orbit camera focused on a target point. Yaw/pitch are accumulated
//  from mouse drag; distance from scroll. Produces a view matrix on demand.
// =============================================================================
#pragma once

#include "visualization/Math.hpp"

namespace whopt {

class Camera {
public:
    void setTarget(const Vec3& t)            { target_ = t; }
    void setDistance(float d)                { distance_ = d; clamp(); }
    void setBounds(float minD, float maxD)   { minDist_ = minD; maxDist_ = maxD; clamp(); }

    void onMouseDrag(float dxPixels, float dyPixels) {
        yaw_   += dxPixels * 0.005f;
        pitch_ += dyPixels * 0.005f;
        const float lim = 1.55f;
        if (pitch_ >  lim) pitch_ =  lim;
        if (pitch_ < -lim + 0.05f) pitch_ = -lim + 0.05f;   // keep above ground
    }
    void onScroll(float dy) {
        const float factor = (dy > 0.f) ? 0.9f : 1.0f / 0.9f;
        distance_ *= factor;
        clamp();
    }
    void onPan(float dxPixels, float dyPixels) {
        // Pan in the camera's local plane.
        const float speed = distance_ * 0.0015f;
        const Vec3 fwd   = forward();
        const Vec3 right = normalize(cross(fwd, Vec3(0,1,0)));
        const Vec3 up    = normalize(cross(right, fwd));
        target_ = target_ + right * (-dxPixels * speed) + up * (dyPixels * speed);
    }

    Vec3 eye() const {
        const float cp = std::cos(pitch_), sp = std::sin(pitch_);
        const float cy = std::cos(yaw_),   sy = std::sin(yaw_);
        return target_ + Vec3(distance_ * cp * sy,
                              distance_ * sp,
                              distance_ * cp * cy);
    }
    Vec3 forward() const { return normalize(target_ - eye()); }

    Mat4 view() const { return Mat4::lookAt(eye(), target_, Vec3(0,1,0)); }

    Mat4 projection(float aspect) const {
        return Mat4::perspective(fovYRad_, aspect, 0.1f, std::max(50.f, distance_ * 50.f));
    }

private:
    Vec3  target_   { 0, 0, 0 };
    float distance_ = 50.0f;
    float yaw_      = 0.6f;
    float pitch_    = 0.6f;
    float fovYRad_  = 0.9f;
    float minDist_  = 1.0f;
    float maxDist_  = 100000.0f;
    void clamp() {
        if (distance_ < minDist_) distance_ = minDist_;
        if (distance_ > maxDist_) distance_ = maxDist_;
    }
};

} // namespace whopt

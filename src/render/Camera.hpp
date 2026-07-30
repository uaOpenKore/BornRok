#pragma once
#include "core/math/Math.hpp"

namespace uaro {

// Helpers for building view/projection matrices. v0 only needs a 2D screen-space
// projection; a 3D map camera arrives with the world renderer (v3).
struct Camera {
    // Top-left origin pixel projection: (0,0) is top-left, (w,h) bottom-right.
    static Mat4 screenOrtho(int w, int h) {
        return Mat4::ortho(0.0f, static_cast<f32>(w), static_cast<f32>(h), 0.0f, -1.0f, 1.0f);
    }
};

} // namespace uaro

#pragma once
#include <cmath>

#include "core/math/Math.hpp"

namespace uaro {

// Camera frustum for culling whole objects before they are submitted. Only the four SIDE planes are
// kept: for a perspective camera they meet at the eye, so together they also reject everything behind
// it, and they don't depend on the depth convention (GL [-1,1] vs D3D/Vulkan [0,1]) or on a mirrored
// (reflection) view. Near/far are left out on purpose - objects are never culled by distance here (LOD
// does that).
struct Frustum {
    Vec4 planes[4];  // left, right, bottom, top; inside when dot(xyz, p) + w >= 0 (xyz normalised)

    // `vp` = proj * view (column vectors, clip = vp * world), as built with Mat4's operator*.
    static Frustum fromViewProj(const Mat4& vp) {
        // Row r of the matrix as a 4-vector: (m[0*4+r], m[1*4+r], m[2*4+r], m[3*4+r]).
        auto row = [&vp](int r) { return Vec4{vp.at(0, r), vp.at(1, r), vp.at(2, r), vp.at(3, r)}; };
        const Vec4 r0 = row(0), r1 = row(1), r3 = row(3);
        Frustum f;
        f.planes[0] = {r3.x + r0.x, r3.y + r0.y, r3.z + r0.z, r3.w + r0.w};  // left:   w + x >= 0
        f.planes[1] = {r3.x - r0.x, r3.y - r0.y, r3.z - r0.z, r3.w - r0.w};  // right:  w - x >= 0
        f.planes[2] = {r3.x + r1.x, r3.y + r1.y, r3.z + r1.z, r3.w + r1.w};  // bottom: w + y >= 0
        f.planes[3] = {r3.x - r1.x, r3.y - r1.y, r3.z - r1.z, r3.w - r1.w};  // top:    w - y >= 0
        for (Vec4& p : f.planes) {
            const f32 len = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
            if (len > 1e-12f) { p.x /= len; p.y /= len; p.z /= len; p.w /= len; }
        }
        return f;
    }

    // False only when the sphere lies entirely outside one plane (conservative: a sphere near a corner
    // may pass while invisible, never the other way round).
    bool sphereVisible(const Vec3& c, f32 r) const {
        for (const Vec4& p : planes)
            if (p.x * c.x + p.y * c.y + p.z * c.z + p.w < -r) return false;
        return true;
    }
};

}  // namespace uaro

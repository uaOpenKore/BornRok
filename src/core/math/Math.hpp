#pragma once
#include <cmath>

#include "core/Types.hpp"

// Compact, self-contained linear algebra for 2D sprites and a simple 3D camera.
// Matrices are 4x4, stored column-major (element [col*4 + row]) to match the
// convention used by OpenGL and bgfx, so they upload without transposition.
namespace uaro {

constexpr f32 kPi = 3.14159265358979323846f;
inline constexpr f32 radians(f32 deg) { return deg * (kPi / 180.0f); }
inline constexpr f32 degrees(f32 rad) { return rad * (180.0f / kPi); }

struct Vec2 {
    f32 x = 0, y = 0;
    Vec2() = default;
    Vec2(f32 x_, f32 y_) : x(x_), y(y_) {}
    Vec2 operator+(Vec2 r) const { return {x + r.x, y + r.y}; }
    Vec2 operator-(Vec2 r) const { return {x - r.x, y - r.y}; }
    Vec2 operator*(f32 s) const { return {x * s, y * s}; }
};

struct Vec3 {
    f32 x = 0, y = 0, z = 0;
    Vec3() = default;
    Vec3(f32 x_, f32 y_, f32 z_) : x(x_), y(y_), z(z_) {}
    Vec3 operator+(const Vec3& r) const { return {x + r.x, y + r.y, z + r.z}; }
    Vec3 operator-(const Vec3& r) const { return {x - r.x, y - r.y, z - r.z}; }
    Vec3 operator*(f32 s) const { return {x * s, y * s, z * s}; }
};

struct Vec4 {
    f32 x = 0, y = 0, z = 0, w = 0;
    Vec4() = default;
    Vec4(f32 x_, f32 y_, f32 z_, f32 w_) : x(x_), y(y_), z(z_), w(w_) {}
};

inline f32 dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline f32 dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline f32 length(const Vec3& v) { return std::sqrt(dot(v, v)); }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline Vec3 normalize(const Vec3& v) {
    f32 len = length(v);
    return len > 0.0f ? v * (1.0f / len) : v;
}

// Column-major 4x4 matrix.
struct Mat4 {
    f32 m[16] = {0};

    f32& at(int col, int row) { return m[col * 4 + row]; }
    f32 at(int col, int row) const { return m[col * 4 + row]; }

    static Mat4 identity() {
        Mat4 r;
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
        return r;
    }

    static Mat4 translation(const Vec3& t) {
        Mat4 r = identity();
        r.m[12] = t.x;
        r.m[13] = t.y;
        r.m[14] = t.z;
        return r;
    }

    static Mat4 scaling(const Vec3& s) {
        Mat4 r;
        r.m[0] = s.x;
        r.m[5] = s.y;
        r.m[10] = s.z;
        r.m[15] = 1.0f;
        return r;
    }

    // Orthographic projection mapping to NDC z in [-1, 1].
    // For a top-left 2D origin use ortho(0, w, h, 0, -1, 1).
    static Mat4 ortho(f32 l, f32 r, f32 b, f32 t, f32 n, f32 f) {
        Mat4 o;
        o.m[0] = 2.0f / (r - l);
        o.m[5] = 2.0f / (t - b);
        o.m[10] = -2.0f / (f - n);
        o.m[12] = -(r + l) / (r - l);
        o.m[13] = -(t + b) / (t - b);
        o.m[14] = -(f + n) / (f - n);
        o.m[15] = 1.0f;
        return o;
    }

    // Right-handed perspective. fovy in radians. homogeneousDepth=true -> NDC z
    // in [-1,1] (OpenGL); false -> [0,1] (Direct3D/Metal/Vulkan). Pass
    // bgfx::getCaps()->homogeneousDepth.
    static Mat4 perspective(f32 fovy, f32 aspect, f32 n, f32 f, bool homogeneousDepth) {
        Mat4 p;
        f32 tanHalf = std::tan(fovy * 0.5f);
        p.m[0] = 1.0f / (aspect * tanHalf);
        p.m[5] = 1.0f / tanHalf;
        p.m[11] = -1.0f;
        if (homogeneousDepth) {
            p.m[10] = (f + n) / (n - f);
            p.m[14] = (2.0f * f * n) / (n - f);
        } else {
            p.m[10] = f / (n - f);
            p.m[14] = (f * n) / (n - f);
        }
        return p;
    }

    static Mat4 rotationX(f32 a) {
        Mat4 r = identity();
        const f32 c = std::cos(a), s = std::sin(a);
        r.m[5] = c; r.m[6] = s; r.m[9] = -s; r.m[10] = c;
        return r;
    }
    static Mat4 rotationY(f32 a) {
        Mat4 r = identity();
        const f32 c = std::cos(a), s = std::sin(a);
        r.m[0] = c; r.m[2] = -s; r.m[8] = s; r.m[10] = c;
        return r;
    }
    static Mat4 rotationZ(f32 a) {
        Mat4 r = identity();
        const f32 c = std::cos(a), s = std::sin(a);
        r.m[0] = c; r.m[1] = s; r.m[4] = -s; r.m[5] = c;
        return r;
    }
    // Rotation of `a` radians about an arbitrary (unit) axis (Rodrigues).
    static Mat4 rotationAxis(f32 a, const Vec3& ax) {
        const f32 len = std::sqrt(ax.x * ax.x + ax.y * ax.y + ax.z * ax.z);
        if (len < 1e-6f) return identity();
        const f32 x = ax.x / len, y = ax.y / len, z = ax.z / len;
        const f32 c = std::cos(a), s = std::sin(a), t = 1.0f - c;
        Mat4 r = identity();
        r.m[0] = t * x * x + c;     r.m[1] = t * x * y + s * z; r.m[2] = t * x * z - s * y;
        r.m[4] = t * x * y - s * z; r.m[5] = t * y * y + c;     r.m[6] = t * y * z + s * x;
        r.m[8] = t * x * z + s * y; r.m[9] = t * y * z - s * x; r.m[10] = t * z * z + c;
        return r;
    }

    // General 4x4 inverse (cofactor method). Returns identity if singular. Used to
    // unproject screen clicks back into the world for click-to-move.
    Mat4 inverse() const {
        const f32* m = this->m;
        f32 inv[16];
        inv[0] = m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
        inv[4] = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
        inv[8] = m[4]*m[9]*m[15] - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
        inv[12] = -m[4]*m[9]*m[14] + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
        inv[1] = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
        inv[5] = m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
        inv[9] = -m[0]*m[9]*m[15] + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
        inv[13] = m[0]*m[9]*m[14] - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
        inv[2] = m[1]*m[6]*m[15] - m[1]*m[7]*m[14] - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7] - m[13]*m[3]*m[6];
        inv[6] = -m[0]*m[6]*m[15] + m[0]*m[7]*m[14] + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7] + m[12]*m[3]*m[6];
        inv[10] = m[0]*m[5]*m[15] - m[0]*m[7]*m[13] - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7] - m[12]*m[3]*m[5];
        inv[14] = -m[0]*m[5]*m[14] + m[0]*m[6]*m[13] + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6] + m[12]*m[2]*m[5];
        inv[3] = -m[1]*m[6]*m[11] + m[1]*m[7]*m[10] + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7] + m[9]*m[3]*m[6];
        inv[7] = m[0]*m[6]*m[11] - m[0]*m[7]*m[10] - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7] - m[8]*m[3]*m[6];
        inv[11] = -m[0]*m[5]*m[11] + m[0]*m[7]*m[9] + m[4]*m[1]*m[11] - m[4]*m[3]*m[9] - m[8]*m[1]*m[7] + m[8]*m[3]*m[5];
        inv[15] = m[0]*m[5]*m[10] - m[0]*m[6]*m[9] - m[4]*m[1]*m[10] + m[4]*m[2]*m[9] + m[8]*m[1]*m[6] - m[8]*m[2]*m[5];
        f32 det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
        if (det == 0.0f) return identity();
        det = 1.0f / det;
        Mat4 r;
        for (int i = 0; i < 16; ++i) r.m[i] = inv[i] * det;
        return r;
    }

    // Right-handed look-at view matrix (camera looks from eye toward at).
    static Mat4 lookAt(const Vec3& eye, const Vec3& at, const Vec3& up) {
        Vec3 f = normalize(at - eye);
        Vec3 s = normalize(cross(f, up));
        Vec3 u = cross(s, f);
        Mat4 m;  // zero
        m.at(0, 0) = s.x;  m.at(1, 0) = s.y;  m.at(2, 0) = s.z;  m.at(3, 0) = -dot(s, eye);
        m.at(0, 1) = u.x;  m.at(1, 1) = u.y;  m.at(2, 1) = u.z;  m.at(3, 1) = -dot(u, eye);
        m.at(0, 2) = -f.x; m.at(1, 2) = -f.y; m.at(2, 2) = -f.z; m.at(3, 2) = dot(f, eye);
        m.at(3, 3) = 1.0f;
        return m;
    }
};

// C = A * B  (apply B first, then A).
inline Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 c;
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row) {
            f32 s = 0;
            for (int k = 0; k < 4; ++k) s += a.m[k * 4 + row] * b.m[col * 4 + k];
            c.m[col * 4 + row] = s;
        }
    return c;
}

inline Vec4 operator*(const Mat4& a, const Vec4& v) {
    return {
        a.m[0] * v.x + a.m[4] * v.y + a.m[8] * v.z + a.m[12] * v.w,
        a.m[1] * v.x + a.m[5] * v.y + a.m[9] * v.z + a.m[13] * v.w,
        a.m[2] * v.x + a.m[6] * v.y + a.m[10] * v.z + a.m[14] * v.w,
        a.m[3] * v.x + a.m[7] * v.y + a.m[11] * v.z + a.m[15] * v.w,
    };
}

} // namespace uaro

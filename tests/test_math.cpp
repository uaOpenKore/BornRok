#include "core/math/Math.hpp"

#include "microtest.hpp"

using namespace uaro;

TEST_CASE(vec_arithmetic) {
    Vec3 a{1, 2, 3};
    Vec3 b{4, 5, 6};
    Vec3 c = a + b;
    CHECK_NEAR(c.x, 5.0, 1e-6);
    CHECK_NEAR(c.y, 7.0, 1e-6);
    CHECK_NEAR(c.z, 9.0, 1e-6);
    CHECK_NEAR(dot(a, b), 32.0, 1e-6);
    CHECK_NEAR(length(Vec3{3, 4, 0}), 5.0, 1e-6);
}

TEST_CASE(vec_cross_and_normalize) {
    Vec3 n = cross(Vec3{1, 0, 0}, Vec3{0, 1, 0});
    CHECK_NEAR(n.z, 1.0, 1e-6);
    Vec3 u = normalize(Vec3{0, 0, 8});
    CHECK_NEAR(length(u), 1.0, 1e-6);
}

TEST_CASE(mat_identity_is_neutral) {
    Mat4 id = Mat4::identity();
    Vec4 v{2, 3, 4, 1};
    Vec4 r = id * v;
    CHECK_NEAR(r.x, 2.0, 1e-6);
    CHECK_NEAR(r.y, 3.0, 1e-6);
    CHECK_NEAR(r.z, 4.0, 1e-6);
    CHECK_NEAR(r.w, 1.0, 1e-6);

    Mat4 m = id * id;
    CHECK_NEAR(m.at(0, 0), 1.0, 1e-6);
    CHECK_NEAR(m.at(1, 1), 1.0, 1e-6);
}

TEST_CASE(mat_translation_moves_point) {
    Mat4 t = Mat4::translation({10, -5, 2});
    Vec4 r = t * Vec4{1, 1, 1, 1};
    CHECK_NEAR(r.x, 11.0, 1e-6);
    CHECK_NEAR(r.y, -4.0, 1e-6);
    CHECK_NEAR(r.z, 3.0, 1e-6);
}

TEST_CASE(mat_ortho_maps_screen_corners_to_ndc) {
    // Top-left origin 2D projection: (0,0) -> (-1,+1), (W,H) -> (+1,-1).
    const f32 W = 800, H = 600;
    Mat4 p = Mat4::ortho(0, W, H, 0, -1, 1);

    Vec4 tl = p * Vec4{0, 0, 0, 1};
    CHECK_NEAR(tl.x, -1.0, 1e-5);
    CHECK_NEAR(tl.y, 1.0, 1e-5);

    Vec4 br = p * Vec4{W, H, 0, 1};
    CHECK_NEAR(br.x, 1.0, 1e-5);
    CHECK_NEAR(br.y, -1.0, 1e-5);
}

TEST_CASE(angle_conversions) {
    CHECK_NEAR(radians(180.0f), kPi, 1e-5);
    CHECK_NEAR(degrees(kPi), 180.0, 1e-4);
}

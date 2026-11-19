// Helpers behind the Switch/L4T performance pass: camera frustum culling, the texture size cap,
// retry back-off, /proc memory parsing for the perf log line, and the receive-buffer cursor.
#include <string>
#include <vector>

#include "core/RetryBackoff.hpp"
#include "core/SysMem.hpp"
#include "core/math/Frustum.hpp"
#include "core/math/Math.hpp"
#include "formats/ImageIO.hpp"
#include "net/Connection.hpp"
#include "net/RxCursor.hpp"

#include "microtest.hpp"

using namespace uaro;

// ---- frustum ---------------------------------------------------------------------------------------

TEST_CASE(frustum_sphere_visibility) {
    const Mat4 proj = Mat4::perspective(radians(60.0f), 16.0f / 9.0f, 0.1f, 1000.0f, true);
    const Mat4 view = Mat4::lookAt({0, 0, 10}, {0, 0, 0}, {0, 1, 0});
    const Frustum f = Frustum::fromViewProj(proj * view);
    CHECK(f.sphereVisible({0, 0, 0}, 1.0f));       // straight ahead
    CHECK(!f.sphereVisible({0, 0, 30}, 1.0f));     // behind the camera
    CHECK(!f.sphereVisible({100, 0, 0}, 1.0f));    // far off to the side
    CHECK(f.sphereVisible({100, 0, 0}, 200.0f));   // a huge sphere still reaches into view
    CHECK(!f.sphereVisible({0, 50, 0}, 1.0f));     // above the top plane at this depth
    // At depth 10 the right edge is at x = tan(30deg) * 10 * 16/9 = 10.26; x = 12 lies ~1.21 beyond it
    // (perpendicular to the plane), so r = 1 is out and r = 3 pokes in.
    CHECK(!f.sphereVisible({12.0f, 0, 0}, 1.0f));
    CHECK(f.sphereVisible({12.0f, 0, 0}, 3.0f));
}

TEST_CASE(frustum_mirrored_view_and_zero_to_one_depth) {
    // The planar water reflection renders with a mirrored view and bgfx may run with [0,1] depth.
    const Mat4 proj = Mat4::perspective(radians(60.0f), 1.0f, 0.1f, 1000.0f, false);
    const Mat4 view = Mat4::lookAt({0, 0, 10}, {0, 0, 0}, {0, 1, 0}) * Mat4::scaling({-1, 1, 1});
    const Frustum f = Frustum::fromViewProj(proj * view);
    CHECK(f.sphereVisible({0, 0, 0}, 1.0f));
    CHECK(!f.sphereVisible({0, 0, 30}, 1.0f));
    CHECK(!f.sphereVisible({-100, 0, 0}, 1.0f));
    CHECK(!f.sphereVisible({100, 0, 0}, 1.0f));
}

// ---- texture size cap --------------------------------------------------------------------------------

namespace {
Image solid(u32 w, u32 h, u8 r, u8 g, u8 b, u8 a) {
    Image img;
    img.width = w;
    img.height = h;
    img.rgba.resize(static_cast<usize>(w) * h * 4);
    for (usize i = 0; i < img.rgba.size(); i += 4) {
        img.rgba[i] = r; img.rgba[i + 1] = g; img.rgba[i + 2] = b; img.rgba[i + 3] = a;
    }
    return img;
}
}  // namespace

TEST_CASE(texcap_halves_until_within_cap) {
    Image img = solid(1024, 256, 200, 100, 50, 255);
    downscaleToMaxDim(img, 512);
    CHECK_EQ(img.width, 512u);
    CHECK_EQ(img.height, 128u);
    CHECK(img.valid());
    CHECK_EQ(img.rgba[0], 200);
    CHECK_EQ(img.rgba[1], 100);
    CHECK_EQ(img.rgba[2], 50);
    CHECK_EQ(img.rgba[3], 255);
    Image big = solid(1600, 1600, 1, 2, 3, 4);
    downscaleToMaxDim(big, 512);  // 1600 -> 800 -> 400
    CHECK_EQ(big.width, 400u);
    CHECK_EQ(big.height, 400u);
}

TEST_CASE(texcap_zero_cap_or_small_image_is_untouched) {
    Image img = solid(256, 256, 9, 9, 9, 9);
    downscaleToMaxDim(img, 0);
    CHECK_EQ(img.width, 256u);
    downscaleToMaxDim(img, 512);
    CHECK_EQ(img.width, 256u);
    downscaleToMaxDim(img, 256);
    CHECK_EQ(img.width, 256u);
    Image bad;  // invalid input: nothing happens
    downscaleToMaxDim(bad, 4);
    CHECK_EQ(bad.width, 0u);
}

TEST_CASE(texcap_box_filters_two_by_two) {
    Image img = solid(2, 2, 0, 0, 0, 255);
    const u8 px[4][4] = {{0, 0, 0, 255}, {100, 0, 0, 255}, {0, 200, 0, 255}, {0, 0, 40, 255}};
    for (int i = 0; i < 4; ++i)
        for (int c = 0; c < 4; ++c) img.rgba[static_cast<usize>(i) * 4 + c] = px[i][c];
    downscaleToMaxDim(img, 1);
    CHECK_EQ(img.width, 1u);
    CHECK_EQ(img.height, 1u);
    CHECK_EQ(img.rgba[0], 25);   // (0+100+0+0)/4
    CHECK_EQ(img.rgba[1], 50);   // (0+0+200+0)/4
    CHECK_EQ(img.rgba[2], 10);   // (0+0+0+40)/4
    CHECK_EQ(img.rgba[3], 255);
}

TEST_CASE(texcap_colour_ignores_transparent_texels) {
    // A cut-out edge: opaque red next to a fully transparent (keyed) magenta texel. The colour of the
    // shrunk texel must stay red (no pink fringe), only the alpha averages.
    Image img = solid(2, 1, 0, 0, 0, 0);
    const u8 px[2][4] = {{255, 0, 0, 255}, {255, 0, 255, 0}};
    for (int i = 0; i < 2; ++i)
        for (int c = 0; c < 4; ++c) img.rgba[static_cast<usize>(i) * 4 + c] = px[i][c];
    downscaleToMaxDim(img, 1);
    CHECK_EQ(img.width, 1u);
    CHECK_EQ(img.height, 1u);
    CHECK_EQ(img.rgba[0], 255);
    CHECK_EQ(img.rgba[1], 0);
    CHECK_EQ(img.rgba[2], 0);
    CHECK_EQ(img.rgba[3], 128);
}

TEST_CASE(texcap_odd_sizes_floor_like_mips) {
    Image img = solid(3, 5, 10, 20, 30, 255);
    downscaleToMaxDim(img, 1);  // 3x5 -> 1x2 -> 1x1
    CHECK_EQ(img.width, 1u);
    CHECK_EQ(img.height, 1u);
    CHECK(img.valid());
    CHECK_EQ(img.rgba[0], 10);
}

// ---- retry back-off -----------------------------------------------------------------------------------

TEST_CASE(backoff_doubles_up_to_cap) {
    RetryBackoff b;
    CHECK(b.due(0.0));
    b.failed(10.0);  // first retry 0.1 s later
    CHECK(!b.due(10.05));
    CHECK(b.due(10.1));
    const float want[] = {0.2f, 0.4f, 0.8f, 1.6f, 2.0f, 2.0f};
    double now = 10.1;
    for (float w : want) {
        b.failed(now);
        CHECK_NEAR(b.delay, w, 1e-6);
        CHECK(!b.due(now + w - 0.01));
        CHECK(b.due(now + w));
        now += w;
    }
    b.reset();
    CHECK(b.due(0.0));
    CHECK_NEAR(b.delay, 0.0, 1e-9);
}

// ---- /proc parsing for the perf line ------------------------------------------------------------------

TEST_CASE(sysmem_parses_meminfo) {
    const std::string text =
        "MemTotal:        3954164 kB\n"
        "MemFree:          112708 kB\n"
        "MemAvailable:     409012 kB\n"
        "Buffers:           10000 kB\n"
        "SwapTotal:       1977076 kB\n"
        "SwapFree:         777076 kB\n";
    const MemInfo m = parseMeminfo(text);
    CHECK_EQ(m.availKb, 409012);
    CHECK_EQ(m.swapUsedKb, 1200000);
    const MemInfo none = parseMeminfo("garbage\n");
    CHECK_EQ(none.availKb, -1);
    CHECK_EQ(none.swapUsedKb, -1);
}

TEST_CASE(sysmem_parses_statm) {
    CHECK_EQ(parseStatmRssPages("524288 131072 2048 512 0 300000 0\n"), 131072);
    CHECK_EQ(parseStatmRssPages(""), -1);
}

// ---- receive-buffer cursor ----------------------------------------------------------------------------

TEST_CASE(rxcursor_consumes_once_on_scope_exit) {
    net::Connection conn;
    conn.rx() = {1, 2, 3, 4, 5, 6};
    {
        net::RxCursor rx(conn);
        CHECK_EQ(rx.size(), 6u);
        rx.consume(2);
        CHECK_EQ(rx.size(), 4u);
        CHECK_EQ(rx[0], 3);
        CHECK_EQ(rx.data()[1], 4);
        CHECK_EQ(conn.rx().size(), 6u);  // nothing erased yet
        rx.consume(1);
    }
    CHECK_EQ(conn.rx().size(), 3u);
    CHECK_EQ(conn.rx()[0], 4);
}

TEST_CASE(rxcursor_flush_hands_back_a_clean_buffer) {
    net::Connection conn;
    conn.rx() = {7, 8, 9};
    net::RxCursor rx(conn);
    rx.consume(3);
    rx.flush();  // e.g. before a reconnect replaces the buffer
    CHECK_EQ(conn.rx().size(), 0u);
    conn.rx() = {1, 2};  // new connection's bytes must not be eaten by the cursor's destructor
    CHECK_EQ(rx.size(), 2u);
}

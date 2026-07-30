#include "formats/Spr.hpp"

#include <vector>

#include "microtest.hpp"

using namespace uaro;

namespace {
void pu8(std::vector<u8>& v, u8 x) { v.push_back(x); }
void pu16(std::vector<u8>& v, u16 x) {
    v.push_back(static_cast<u8>(x & 0xff));
    v.push_back(static_cast<u8>((x >> 8) & 0xff));
}
void append_palette(std::vector<u8>& v, const std::vector<std::pair<u8, SprPalColor>>& entries) {
    std::vector<u8> pal(1024, 0);
    for (const auto& [idx, c] : entries) {
        pal[idx * 4 + 0] = c.r;
        pal[idx * 4 + 1] = c.g;
        pal[idx * 4 + 2] = c.b;
        pal[idx * 4 + 3] = c.a;
    }
    v.insert(v.end(), pal.begin(), pal.end());
}
} // namespace

TEST_CASE(spr_raw_indexed_v100) {
    std::vector<u8> b{'S', 'P'};
    pu16(b, 0x100);
    pu16(b, 1);          // indexed count (no rgba field for ver < 0x101)
    pu16(b, 2);          // frame width
    pu16(b, 2);          // frame height
    for (u8 px : {0, 1, 2, 1}) pu8(b, px);
    append_palette(b, {{1, {10, 20, 30, 0}}, {2, {40, 50, 60, 0}}});

    auto spr = Sprite::parse(b);
    CHECK(spr.has_value());
    if (!spr) return;
    CHECK_EQ(spr->version(), 0x100u);
    CHECK_EQ(spr->indexedFrames().size(), static_cast<usize>(1));
    CHECK(spr->hasPalette());

    const SprFrame& f = spr->indexedFrames()[0];
    CHECK_EQ(f.width, 2);
    CHECK_EQ(f.height, 2);
    CHECK_EQ(f.pixels.size(), static_cast<usize>(4));
    CHECK_EQ(f.pixels[2], 2);

    auto rgba = spr->indexedToRgba(0);
    CHECK_EQ(rgba.size(), static_cast<usize>(16));
    // index 0 -> transparent
    CHECK_EQ(rgba[3], 0);
    // index 1 -> palette[1], opaque
    CHECK_EQ(rgba[4], 10);
    CHECK_EQ(rgba[5], 20);
    CHECK_EQ(rgba[6], 30);
    CHECK_EQ(rgba[7], 255);
    // index 2 -> palette[2]
    CHECK_EQ(rgba[8], 40);
    CHECK_EQ(rgba[11], 255);
}

TEST_CASE(spr_rle_indexed_v201) {
    std::vector<u8> b{'S', 'P'};
    pu16(b, 0x201);
    pu16(b, 1);  // indexed count
    pu16(b, 0);  // rgba count (present for ver >= 0x101)
    pu16(b, 4);  // width
    pu16(b, 1);  // height
    // RLE for pixels [5,0,0,5]: literal 5; (0, count=2) -> two zeros; literal 5
    pu16(b, 4);  // data length
    for (u8 x : {5, 0, 2, 5}) pu8(b, x);
    append_palette(b, {{5, {1, 2, 3, 0}}});

    auto spr = Sprite::parse(b);
    CHECK(spr.has_value());
    if (!spr) return;
    CHECK_EQ(spr->version(), 0x201u);
    const SprFrame& f = spr->indexedFrames()[0];
    CHECK_EQ(f.pixels.size(), static_cast<usize>(4));
    CHECK_EQ(f.pixels[0], 5);
    CHECK_EQ(f.pixels[1], 0);
    CHECK_EQ(f.pixels[2], 0);
    CHECK_EQ(f.pixels[3], 5);
}

TEST_CASE(spr_truecolor_v101) {
    std::vector<u8> b{'S', 'P'};
    pu16(b, 0x101);
    pu16(b, 0);  // indexed count
    pu16(b, 1);  // rgba count
    pu16(b, 1);  // width
    pu16(b, 1);  // height
    // Raw truecolor pixels are stored ABGR (alpha first); the parser flips them to RGBA.
    for (u8 x : {200, 100, 50, 255}) pu8(b, x);  // A=200, B=100, G=50, R=255

    auto spr = Sprite::parse(b);
    CHECK(spr.has_value());
    if (!spr) return;
    CHECK_EQ(spr->rgbaFrames().size(), static_cast<usize>(1));
    CHECK(!spr->hasPalette());
    const SprFrame& f = spr->rgbaFrames()[0];
    CHECK_EQ(f.pixels.size(), static_cast<usize>(4));
    CHECK_EQ(f.pixels[0], 255);  // R
    CHECK_EQ(f.pixels[1], 50);   // G
    CHECK_EQ(f.pixels[2], 100);  // B
    CHECK_EQ(f.pixels[3], 200);  // A
}

TEST_CASE(spr_truecolor_rows_flipped_vertically) {
    // RO truecolor frames are stored bottom-up; parse() flips the rows so row 0 is the top.
    std::vector<u8> b{'S', 'P'};
    pu16(b, 0x101);
    pu16(b, 0);  // indexed count
    pu16(b, 1);  // rgba count
    pu16(b, 1);  // width
    pu16(b, 2);  // height (2 rows)
    for (u8 x : {10, 20, 30, 40}) pu8(b, x);   // stored row 0 -> RGBA [40,30,20,10]
    for (u8 x : {50, 60, 70, 80}) pu8(b, x);   // stored row 1 -> RGBA [80,70,60,50]

    auto spr = Sprite::parse(b);
    CHECK(spr.has_value());
    if (!spr) return;
    const SprFrame& f = spr->rgbaFrames()[0];
    CHECK_EQ(f.pixels.size(), static_cast<usize>(8));
    // After the vertical flip, the stored LAST row is now on top.
    CHECK_EQ(f.pixels[0], 80);   // top row R (was stored row 1)
    CHECK_EQ(f.pixels[3], 50);   // top row A
    CHECK_EQ(f.pixels[4], 40);   // bottom row R (was stored row 0)
    CHECK_EQ(f.pixels[7], 10);   // bottom row A
}

TEST_CASE(spr_swap_truecolor_red_blue) {
    // swapTruecolorRedBlue fixes ARGB-stored effect sprites (e.g. the warp portal) whose blue
    // decodes as red: it swaps R<->B on every truecolor pixel, leaving G and A untouched.
    std::vector<u8> b{'S', 'P'};
    pu16(b, 0x101);
    pu16(b, 0);  // indexed count
    pu16(b, 1);  // rgba count
    pu16(b, 1);  // width
    pu16(b, 1);  // height
    for (u8 x : {200, 100, 50, 255}) pu8(b, x);  // -> RGBA [255,50,100,200]

    auto spr = Sprite::parse(b);
    CHECK(spr.has_value());
    if (!spr) return;
    spr->swapTruecolorRedBlue();
    const SprFrame& f = spr->rgbaFrames()[0];
    CHECK_EQ(f.pixels[0], 100);  // R was 255 -> now old B
    CHECK_EQ(f.pixels[1], 50);   // G unchanged
    CHECK_EQ(f.pixels[2], 255);  // B was 100 -> now old R
    CHECK_EQ(f.pixels[3], 200);  // A unchanged
}

TEST_CASE(spr_drop_indexed_frames) {
    // dropIndexedFrames keeps the truecolor frames and clears the indexed ones (warp portal: keep
    // only the ripple, drop the stone-ring base).
    std::vector<u8> b{'S', 'P'};
    pu16(b, 0x101);
    pu16(b, 0);  // indexed count
    pu16(b, 1);  // rgba count
    pu16(b, 1);
    pu16(b, 1);
    for (u8 x : {200, 100, 50, 255}) pu8(b, x);
    auto spr = Sprite::parse(b);
    CHECK(spr.has_value());
    if (!spr) return;
    spr->dropIndexedFrames();
    CHECK_EQ(spr->indexedFrames().size(), static_cast<usize>(0));
    CHECK_EQ(spr->rgbaFrames().size(), static_cast<usize>(1));  // ripple kept
}

TEST_CASE(spr_rejects_bad_signature) {
    std::vector<u8> b{'X', 'Y', 0, 1, 0, 0};
    CHECK(!Sprite::parse(b).has_value());
}

TEST_CASE(pal_parse_256_rgba) {
    std::vector<u8> b(1024);
    for (usize i = 0; i < 256; ++i) {
        b[i * 4 + 0] = static_cast<u8>(i);        // R
        b[i * 4 + 1] = static_cast<u8>(255 - i);  // G
        b[i * 4 + 2] = static_cast<u8>(i / 2);    // B
        b[i * 4 + 3] = static_cast<u8>(i % 7);    // A
    }
    auto pal = parsePal(b);
    CHECK(pal.has_value());
    if (!pal) return;
    CHECK_EQ((*pal)[0].r, 0);
    CHECK_EQ((*pal)[5].r, 5);
    CHECK_EQ((*pal)[5].g, 250);
    CHECK_EQ((*pal)[255].r, 255);
    CHECK_EQ((*pal)[255].b, 127);
}

TEST_CASE(pal_rejects_short) {
    CHECK(!parsePal(std::vector<u8>(1000)).has_value());  // a .pal must be a full 1024 bytes
}

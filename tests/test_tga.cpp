#include "formats/Tga.hpp"

#include <vector>

#include "microtest.hpp"

using namespace uaro;

namespace {
void pu8(std::vector<u8>& v, u8 x) { v.push_back(x); }
void pu16(std::vector<u8>& v, u16 x) {
    v.push_back(static_cast<u8>(x & 0xff));
    v.push_back(static_cast<u8>((x >> 8) & 0xff));
}
// An 18-byte TGA header for a true-color image.
void header(std::vector<u8>& v, u8 imageType, u16 w, u16 h, u8 bpp, u8 descriptor) {
    pu8(v, 0);          // idLength
    pu8(v, 0);          // colorMapType
    pu8(v, imageType);  // 2 = uncompressed, 10 = RLE
    pu16(v, 0);         // colour-map start
    pu16(v, 0);         // colour-map length
    pu8(v, 0);          // colour-map depth
    pu16(v, 0);         // x origin
    pu16(v, 0);         // y origin
    pu16(v, w);
    pu16(v, h);
    pu8(v, bpp);
    pu8(v, descriptor);
}
} // namespace

// 2x2 32-bit uncompressed TGA, bottom-up (descriptor bit5=0 -> flip to top-down).
// File rows (BGRA): bottom row = blue, white; top row = red, green.
TEST_CASE(tga_decodes_32bit_bottom_up) {
    std::vector<u8> t;
    header(t, 2, 2, 2, 32, 0);
    // bottom image row first
    pu8(t, 255); pu8(t, 0); pu8(t, 0); pu8(t, 255);     // blue  (B,G,R,A)
    pu8(t, 255); pu8(t, 255); pu8(t, 255); pu8(t, 255); // white
    // top image row
    pu8(t, 0); pu8(t, 0); pu8(t, 255); pu8(t, 255);     // red
    pu8(t, 0); pu8(t, 255); pu8(t, 0); pu8(t, 255);     // green

    auto img = Tga::decode(t);
    CHECK(img.has_value());
    if (!img) return;
    CHECK(img->valid());
    CHECK_EQ(img->width, 2u);
    CHECK_EQ(img->height, 2u);
    // (0,0) top-left = red
    CHECK_EQ(img->rgba[0], 255);
    CHECK_EQ(img->rgba[1], 0);
    CHECK_EQ(img->rgba[2], 0);
    CHECK_EQ(img->rgba[3], 255);
    // (1,0) top-right = green
    CHECK_EQ(img->rgba[4], 0);
    CHECK_EQ(img->rgba[5], 255);
    // (0,1) bottom-left = blue
    CHECK_EQ(img->rgba[(2 * 1 + 0) * 4 + 2], 255);
    // (1,1) bottom-right = white
    CHECK_EQ(img->rgba[(2 * 1 + 1) * 4 + 0], 255);
    CHECK_EQ(img->rgba[(2 * 1 + 1) * 4 + 1], 255);
    CHECK_EQ(img->rgba[(2 * 1 + 1) * 4 + 2], 255);
}

// 1x1 24-bit magenta (255,0,255) -> keyed to transparent (alpha 0).
TEST_CASE(tga_keys_magenta_24bit) {
    std::vector<u8> t;
    header(t, 2, 1, 1, 24, 0x20);  // top-down, no flip
    pu8(t, 255); pu8(t, 0); pu8(t, 255);  // B,G,R = magenta
    auto img = Tga::decode(t);
    CHECK(img.has_value());
    if (!img) return;
    CHECK_EQ(img->rgba[0], 255);  // R
    CHECK_EQ(img->rgba[1], 0);    // G
    CHECK_EQ(img->rgba[2], 255);  // B
    CHECK_EQ(img->rgba[3], 0);    // A: magenta -> transparent
}

// 2x2 32-bit RLE: one run packet of 4 red pixels.
TEST_CASE(tga_decodes_rle) {
    std::vector<u8> t;
    header(t, 10, 2, 2, 32, 0x20);  // top-down
    pu8(t, 0x83);                    // RLE packet, run length = (3)+1 = 4
    pu8(t, 0); pu8(t, 0); pu8(t, 255); pu8(t, 255);  // B,G,R,A = red
    auto img = Tga::decode(t);
    CHECK(img.has_value());
    if (!img) return;
    CHECK_EQ(img->width, 2u);
    CHECK_EQ(img->height, 2u);
    for (int i = 0; i < 4; ++i) {
        CHECK_EQ(img->rgba[i * 4 + 0], 255);  // R
        CHECK_EQ(img->rgba[i * 4 + 1], 0);    // G
        CHECK_EQ(img->rgba[i * 4 + 2], 0);    // B
        CHECK_EQ(img->rgba[i * 4 + 3], 255);  // A
    }
}

TEST_CASE(tga_rejects_unsupported_type) {
    std::vector<u8> t;
    header(t, 1, 1, 1, 8, 0);  // colour-mapped, 8bpp -> unsupported
    CHECK(!Tga::decode(t).has_value());
}

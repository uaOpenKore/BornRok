#include "formats/Bmp.hpp"

#include <vector>

#include "microtest.hpp"

using namespace uaro;

namespace {
void pu8(std::vector<u8>& v, u8 x) { v.push_back(x); }
void pu16(std::vector<u8>& v, u16 x) {
    v.push_back(static_cast<u8>(x & 0xff));
    v.push_back(static_cast<u8>((x >> 8) & 0xff));
}
void pu32(std::vector<u8>& v, u32 x) {
    for (int i = 0; i < 4; ++i) v.push_back(static_cast<u8>((x >> (i * 8)) & 0xff));
}
} // namespace

// 2x2 24-bit BMP, bottom-up. Bottom row: blue, white. Top row: red, green.
TEST_CASE(bmp_decodes_24bit) {
    std::vector<u8> b{'B', 'M'};
    pu32(b, 70);  // file size
    pu32(b, 0);   // reserved
    pu32(b, 54);  // data offset
    pu32(b, 40);  // DIB size
    pu32(b, 2);   // width
    pu32(b, 2);   // height
    pu16(b, 1);   // planes
    pu16(b, 24);  // bpp
    pu32(b, 0);   // compression BI_RGB
    pu32(b, 0);   // image size
    pu32(b, 0);   // xPPM
    pu32(b, 0);   // yPPM
    pu32(b, 0);   // colors used
    pu32(b, 0);   // colors important
    // pixel data (B,G,R), bottom row first, rows padded to 4 bytes
    // bottom: blue(0,0,255) white(255,255,255)
    pu8(b, 255); pu8(b, 0); pu8(b, 0);     // blue
    pu8(b, 255); pu8(b, 255); pu8(b, 255); // white
    pu8(b, 0); pu8(b, 0);                  // padding
    // top: red(255,0,0) green(0,255,0)
    pu8(b, 0); pu8(b, 0); pu8(b, 255);     // red
    pu8(b, 0); pu8(b, 255); pu8(b, 0);     // green
    pu8(b, 0); pu8(b, 0);                  // padding

    auto img = Bmp::decode(b);
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

TEST_CASE(bmp_rejects_bad_signature) {
    std::vector<u8> b(54, 0);
    b[0] = 'X';
    CHECK(!Bmp::decode(b).has_value());
}

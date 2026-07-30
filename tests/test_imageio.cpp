#include "formats/ImageIO.hpp"

#include <string>
#include <vector>

#include "microtest.hpp"

using namespace uaro;

TEST_CASE(imageio_normal_map_path) {
    // Extension replaced with _n.png (S.'s "texture_n.png" convention).
    CHECK_EQ(normalMapPath("data/texture/foo.bmp"), std::string("data/texture/foo_n.png"));
    CHECK_EQ(normalMapPath("data/sprite/bar.png"), std::string("data/sprite/bar_n.png"));
    CHECK_EQ(normalMapPath("baz.tga"), std::string("baz_n.png"));
    // No extension -> just append.
    CHECK_EQ(normalMapPath("noext"), std::string("noext_n.png"));
    // A dot in a directory but none in the filename must not be treated as an extension.
    CHECK_EQ(normalMapPath("a.b/qux"), std::string("a.b/qux_n.png"));
}

TEST_CASE(imageio_decode_png) {
    // A minimal 1x1 opaque-red PNG (magic-routed to the stb path).
    const std::vector<u8> png = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48,
        0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x02, 0x00, 0x00,
        0x00, 0x90, 0x77, 0x53, 0xDE, 0x00, 0x00, 0x00, 0x0C, 0x49, 0x44, 0x41, 0x54, 0x08,
        0xD7, 0x63, 0xF8, 0xCF, 0xC0, 0x00, 0x00, 0x00, 0x03, 0x00, 0x01, 0x6E, 0xF4, 0x8D,
        0x8C, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};
    auto img = decodeImage(png);
    CHECK(img.has_value());
    CHECK_EQ(img->width, 1u);
    CHECK_EQ(img->height, 1u);
    CHECK_EQ(img->rgba.size(), 4u);
    CHECK_EQ(img->rgba[0], 255);  // R
    CHECK_EQ(img->rgba[1], 0);    // G
    CHECK_EQ(img->rgba[2], 0);    // B
}

TEST_CASE(imageio_key_and_despill_magenta) {
    // 3x1: [pure magenta key][pink AA rim, adjacent to key][clean green leaf].
    Image im;
    im.width = 3;
    im.height = 1;
    im.rgba = {255, 0, 255, 255,   // key
               170, 90, 170, 255,  // rim (magenta-tinted, G>=64 so hard key spares it)
               60, 150, 40, 255};  // leaf
    const usize keyed = keyAndDespillMagenta(im);
    CHECK_EQ(keyed, 1u);
    // Key -> fully transparent black.
    CHECK_EQ(im.rgba[0], 0);
    CHECK_EQ(im.rgba[3], 0);
    // Rim -> magenta excess (min(R,B)-G = 80) removed from R/B, alpha faded by it.
    CHECK_EQ(im.rgba[4], 90);   // 170-80
    CHECK_EQ(im.rgba[6], 90);   // 170-80
    CHECK_EQ(im.rgba[7], 175);  // 255-80: dissolves toward transparent
    // Leaf -> untouched.
    CHECK_EQ(im.rgba[8], 60);
    CHECK_EQ(im.rgba[9], 150);
    CHECK_EQ(im.rgba[11], 255);
}

TEST_CASE(imageio_despill_leaves_solid_purple_alone) {
    // A solid purple region with NO neighbouring pure-magenta key must be 100% untouched (no key
    // present at all -> the function returns early), so real purple/pink textures never get eaten.
    Image im;
    im.width = 2;
    im.height = 1;
    im.rgba = {128, 0, 128, 255, 128, 0, 128, 255};
    CHECK_EQ(keyAndDespillMagenta(im), 0u);
    CHECK_EQ(im.rgba[0], 128);
    CHECK_EQ(im.rgba[2], 128);
    CHECK_EQ(im.rgba[3], 255);
    CHECK_EQ(im.rgba[7], 255);
}

TEST_CASE(imageio_normal_from_luminance) {
    // A vertical brightness edge (dark left half, bright right) must tilt normals along X
    // at the seam and stay flat (128,128,255) far from it.
    Image img;
    img.width = 8;
    img.height = 8;
    img.rgba.resize(8 * 8 * 4);
    for (u32 y = 0; y < 8; ++y)
        for (u32 x = 0; x < 8; ++x) {
            u8* p = &img.rgba[(y * 8 + x) * 4];
            const u8 v = x < 4 ? 40 : 220;
            p[0] = p[1] = p[2] = v;
            p[3] = 255;
        }
    const Image n = normalFromLuminance(img, 2.0f);
    CHECK(n.valid());
    CHECK_EQ(n.width, 8u);
    // Far from the edge: flat normal.
    const u8* flat = &n.rgba[(4 * 8 + 1) * 4];
    CHECK(flat[0] > 120 && flat[0] < 136);
    CHECK(flat[2] > 240);
    // At the edge (x=3..4): X component pushed off-centre, Z reduced.
    const u8* edge = &n.rgba[(4 * 8 + 3) * 4];
    CHECK(edge[0] < 110);       // normal leans -X (brightness rises toward +X)
    CHECK(edge[2] < flat[2]);   // less "straight up" than the flat area
    // Invalid input -> invalid output.
    CHECK(!normalFromLuminance(Image{}, 2.0f).valid());
}

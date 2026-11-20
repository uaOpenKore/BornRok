#include "formats/SprDye.hpp"

#include "microtest.hpp"

using namespace uaro;

namespace {
SprPalColor rgb(u8 r, u8 g, u8 b) { return SprPalColor{r, g, b, 255}; }
// A 2x2 indexed guide (1k) and its 4x4 truecolor frame (2k): index 1 = cloth (red), 2 = skin, 0 = clear.
//   guide:  1 2      webp: each guide pixel -> 2x2 block, cloth pixels with +-6 shading noise
//           0 1
Sprite guideSprite(const std::array<SprPalColor, 256>& pal) {
    SprFrame f;
    f.width = 2;
    f.height = 2;
    f.pixels = {1, 2, 0, 1};
    return Sprite::fromIndexedFrames({f}, pal);
}
Sprite webpSprite(const std::array<SprPalColor, 256>& pal) {
    SprFrame f;
    f.width = 4;
    f.height = 4;
    f.pixels.resize(4 * 4 * 4);
    const u8 g[4] = {1, 2, 0, 1};
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) {
            const u8 idx = g[(y / 2) * 2 + x / 2];
            u8* p = &f.pixels[(y * 4 + x) * 4];
            if (idx == 0) { p[0] = p[1] = p[2] = p[3] = 0; continue; }
            const int n = ((x + y) & 1) ? 6 : -6;  // shading the dye must keep
            p[0] = static_cast<u8>(pal[idx].r + n);
            p[1] = static_cast<u8>(pal[idx].g + n);
            p[2] = static_cast<u8>(pal[idx].b + n);
            p[3] = 255;
        }
    return Sprite::fromRgbaFrames({f}, 0.5f, 1);
}
}  // namespace

TEST_CASE(sprdye_webp_takes_the_dye_through_the_1k_indices) {
    std::array<SprPalColor, 256> pal{};
    pal[1] = rgb(200, 60, 60);    // cloth
    pal[2] = rgb(230, 180, 150);  // skin
    std::array<SprPalColor, 256> dye = pal;
    dye[1] = rgb(40, 60, 220);  // the dye turns the cloth blue, skin stays
    Sprite g = guideSprite(pal);
    Sprite w = webpSprite(pal);
    CHECK(dyeTruecolorFromGuide(w, g, dye) == 1);  // one frame recoloured
    const auto& px = w.rgbaFrames()[0].pixels;
    auto at = [&](int x, int y) { return &px[(y * 4 + x) * 4]; };
    // cloth (0,0): the blue dye + its own -6 shading
    CHECK(at(0, 0)[0] == 40 - 6);
    CHECK(at(0, 0)[1] == 60 - 6);
    CHECK(at(0, 0)[2] == 220 - 6);
    // cloth (1,0): +6 shading kept
    CHECK(at(1, 0)[2] == 220 + 6);
    // skin (2,0) untouched
    CHECK(at(2, 0)[0] == 230 - 6);
    CHECK(at(2, 0)[1] == 180 - 6);
    // transparent stays transparent
    CHECK(at(0, 2)[3] == 0);
    CHECK(at(0, 2)[2] == 0);
    // bottom-right cloth block recoloured too
    CHECK(at(3, 3)[2] == 220 - 6);
}

TEST_CASE(sprdye_skips_frames_that_do_not_line_up) {
    std::array<SprPalColor, 256> pal{};
    pal[1] = rgb(200, 60, 60);
    std::array<SprPalColor, 256> dye = pal;
    dye[1] = rgb(0, 0, 255);
    Sprite g = guideSprite(pal);
    SprFrame odd;
    odd.width = 5;  // not a whole multiple of the guide's 2 px
    odd.height = 4;
    odd.pixels.assign(5 * 4 * 4, 200);
    Sprite w = Sprite::fromRgbaFrames({odd}, 0.5f, 1);
    CHECK(dyeTruecolorFromGuide(w, g, dye) == 0);
    CHECK(w.rgbaFrames()[0].pixels[0] == 200);
}

TEST_CASE(sprdye_identity_dye_changes_nothing) {
    std::array<SprPalColor, 256> pal{};
    pal[1] = rgb(200, 60, 60);
    pal[2] = rgb(230, 180, 150);
    Sprite g = guideSprite(pal);
    Sprite w = webpSprite(pal);
    const auto before = w.rgbaFrames()[0].pixels;
    dyeTruecolorFromGuide(w, g, pal);
    CHECK(w.rgbaFrames()[0].pixels == before);
}

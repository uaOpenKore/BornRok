#include "microtest.hpp"
#include "resource/BodyPalRemap.hpp"

#include <array>
#include <string>
#include <vector>

using namespace uaro;

namespace {

SprPalColor rgb(u8 r, u8 g, u8 b) { return SprPalColor{r, g, b, 0}; }
bool sameRgb(const SprPalColor& a, const SprPalColor& b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

struct Sample { u8 idx, r, g, b; };  // a sprite.zip palette index and the colour uaRO.exe shows there
struct RealBody {
    const char* body;
    bool male;
    u64 fingerprint;            // embedded palette of the sprite.zip body (= the table entry's)
    std::vector<Sample> pixels;  // pixels of 3 frames: sprite.zip index -> uaRO.exe colour for dye 5
    std::vector<Sample> dye;     // the dye palette entries those pixels use (uaRO indices)
};

// Derived from the exe reference: the uaRO body rendered with data/palette/몸/<name>_<sex>_5.pal, sampled on
// frames whose art is unchanged in sprite.zip (first / middle / last). Without the remap 11, 12 and 8 of the
// 12 pixels would come out in the wrong colour.
const std::vector<RealBody> kRealBodies = {
    {"\303\312\272\270\300\332", false, 0xe4313d7a39eed537ULL,  // 초보자_여, dye 초보자_여_5.pal
     {{21, 75, 72, 106}, {71, 50, 51, 64}, {22, 61, 61, 83}, {37, 189, 115, 107}, {71, 50, 51, 64},
      {21, 75, 72, 106}, {47, 41, 39, 59}, {30, 145, 106, 99}, {65, 153, 146, 190}, {67, 111, 103, 150},
      {64, 174, 170, 209}, {31, 130, 83, 83}},
     {{14, 145, 106, 99}, {15, 130, 83, 83}, {95, 41, 39, 59}, {117, 75, 72, 106}, {118, 61, 61, 83},
      {133, 189, 115, 107}, {168, 174, 170, 209}, {169, 153, 146, 190}, {171, 111, 103, 150}, {175, 50, 51, 64}}},
    {"\261\342\273\347", true, 0x705f2699525a9776ULL,  // 기사_남, dye 기사_남_5.pal
     {{37, 75, 72, 106}, {35, 111, 103, 150}, {36, 90, 84, 130}, {52, 220, 144, 132}, {37, 75, 72, 106},
      {30, 150, 111, 102}, {79, 41, 39, 59}, {31, 130, 83, 83}, {87, 41, 39, 59}, {85, 96, 89, 114},
      {84, 122, 111, 140}, {43, 202, 183, 175}},
     {{14, 150, 111, 102}, {15, 130, 83, 83}, {115, 111, 103, 150}, {116, 90, 84, 130}, {117, 75, 72, 106},
      {123, 202, 183, 175}, {132, 220, 144, 132}, {175, 41, 39, 59}, {196, 122, 111, 140}, {197, 96, 89, 114},
      {199, 41, 39, 59}}},
    {"\260\313\273\347", true, 0x1236ba72bfc62963ULL,  // 검사_남, dye 검사_남_5.pal
     {{22, 61, 61, 83}, {21, 75, 72, 106}, {23, 50, 51, 64}, {13, 163, 129, 113}, {22, 61, 61, 83},
      {2, 50, 51, 64}, {55, 50, 51, 64}, {37, 189, 115, 107}, {2, 50, 51, 64}, {51, 111, 103, 150},
      {55, 50, 51, 64}, {15, 130, 83, 83}},
     {{2, 50, 51, 64}, {13, 163, 129, 113}, {15, 130, 83, 83}, {117, 75, 72, 106}, {118, 61, 61, 83},
      {119, 50, 51, 64}, {133, 189, 115, 107}, {195, 111, 103, 150}, {199, 50, 51, 64}}},
};

}  // namespace

TEST_CASE(bodypalremap_applies_the_map) {
    std::array<SprPalColor, 256> dye{};
    for (usize i = 0; i < 256; ++i) dye[i] = rgb(static_cast<u8>(i), static_cast<u8>(255 - i), 7);
    u8 map[256];
    for (usize i = 0; i < 256; ++i) map[i] = static_cast<u8>(i);
    map[1] = 200;  // sprite index 1 is the uaRO index 200
    map[5] = 3;
    map[255] = 1;
    const auto eff = remapPalette(dye, map);
    CHECK(sameRgb(eff[0], dye[0]));  // index 0 (transparent) keeps its own entry
    CHECK(sameRgb(eff[1], dye[200]));
    CHECK(sameRgb(eff[5], dye[3]));
    CHECK(sameRgb(eff[255], dye[1]));
    CHECK(sameRgb(eff[2], dye[2]));  // identity rows stay

    u8 identity[256];
    for (usize i = 0; i < 256; ++i) identity[i] = static_cast<u8>(i);
    const auto same = remapPalette(dye, identity);
    bool allSame = true;
    for (usize i = 0; i < 256; ++i) allSame = allSame && sameRgb(same[i], dye[i]);
    CHECK(allSame);
}

TEST_CASE(bodypalremap_fingerprint_is_fnv1a_of_rgb) {
    std::array<SprPalColor, 256> pal{};
    CHECK_EQ(sprPaletteFingerprint(pal), 0x9fa9e040e0eedf25ULL);  // 768 zero bytes
    pal[7].g = 200;
    CHECK_EQ(sprPaletteFingerprint(pal), 0x10d6726e38be031dULL);
    pal[7].a = 99;  // alpha is not part of the fingerprint
    CHECK_EQ(sprPaletteFingerprint(pal), 0x10d6726e38be031dULL);
}

TEST_CASE(bodypalremap_lookup_needs_body_sex_and_fingerprint) {
    CHECK(kBodyPalRemapCount > 0);
    const RealBody& nov = kRealBodies[0];
    const BodyPalRemap* female = findBodyPalRemap(nov.body, nov.male, nov.fingerprint);
    CHECK(female != nullptr && !female->male);
    CHECK(findBodyPalRemap(nov.body, nov.male, nov.fingerprint ^ 1) == nullptr);  // another sprite.zip body
    // Both Novice bodies share one palette in sprite.zip (same fingerprint): the sex picks the entry.
    const BodyPalRemap* male = findBodyPalRemap(nov.body, !nov.male, nov.fingerprint);
    CHECK(male != nullptr && male != female && male->male);
    CHECK(findBodyPalRemap("\260\341\310\245", true, nov.fingerprint) == nullptr);  // 결혼: identical, no map
    for (usize i = 0; i < kBodyPalRemapCount; ++i) CHECK_EQ(kBodyPalRemaps[i].map[0], 0);
}

TEST_CASE(bodypalremap_real_bodies_get_uaro_exe_colours) {
    for (const RealBody& b : kRealBodies) {
        const BodyPalRemap* rm = findBodyPalRemap(b.body, b.male, b.fingerprint);
        CHECK(rm != nullptr);
        if (!rm) continue;
        std::array<SprPalColor, 256> dye{};
        dye.fill(rgb(1, 2, 3));  // entries the samples don't use: a colour none of them expects
        for (const Sample& d : b.dye) dye[d.idx] = rgb(d.r, d.g, d.b);
        const auto eff = remapPalette(dye, rm->map);
        for (const Sample& p : b.pixels) CHECK(sameRgb(eff[p.idx], rgb(p.r, p.g, p.b)));
    }
}

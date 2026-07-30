#include "formats/PngSprite.hpp"

#include <string>
#include <vector>

#include "microtest.hpp"

using namespace uaro;

namespace {

// 1x1 opaque-red RGB PNG (same fixture as test_imageio).
const std::vector<u8> kRed1x1 = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48,
    0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x02, 0x00, 0x00,
    0x00, 0x90, 0x77, 0x53, 0xDE, 0x00, 0x00, 0x00, 0x0C, 0x49, 0x44, 0x41, 0x54, 0x08,
    0xD7, 0x63, 0xF8, 0xCF, 0xC0, 0x00, 0x00, 0x00, 0x03, 0x00, 0x01, 0x6E, 0xF4, 0x8D,
    0x8C, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};

// 4x4 opaque-green RGBA PNG.
const std::vector<u8> kGreen4x4 = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48,
    0x44, 0x52, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x04, 0x08, 0x06, 0x00, 0x00,
    0x00, 0xA9, 0xF1, 0x9E, 0x7E, 0x00, 0x00, 0x00, 0x0F, 0x49, 0x44, 0x41, 0x54, 0x78,
    0x9C, 0x63, 0x60, 0xF8, 0x8F, 0x06, 0x49, 0x17, 0x00, 0x00, 0x2C, 0x50, 0x1F, 0xE1,
    0xFC, 0xD4, 0x8F, 0x57, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42,
    0x60, 0x82};

auto files(std::vector<std::pair<std::string, const std::vector<u8>*>> fs) {
    return [fs](const std::string& name) -> std::optional<std::vector<u8>> {
        for (const auto& [n, b] : fs)
            if (n == name) return *b;
        return std::nullopt;
    };
}

}  // namespace

TEST_CASE(pngsprite_basic_two_actions) {
    const std::string m =
        "# test sprite\n"
        "action idle 150\n"
        "frame a.png\n"
        "frame b.png\n"
        "action walk 100\n"
        "frame b.png\n";
    std::string err;
    auto r = parsePngSprite(m, files({{"a.png", &kRed1x1}, {"b.png", &kGreen4x4}}), &err);
    CHECK(r.has_value());
    if (!r) return;
    // Two distinct frames decoded; b.png reused (not decoded twice).
    CHECK_EQ(r->sprite.rgbaFrames().size(), 2u);
    CHECK_EQ(r->sprite.indexedFrames().size(), 0u);
    // Layout: motions 0..1, 8 directions each.
    CHECK_EQ(r->action.actions().size(), 16u);
    const auto& idle = r->action.actions()[0];        // motion 0, dir 0
    const auto& walk = r->action.actions()[1 * 8 + 3];  // motion 1, dir 3 (replicated)
    CHECK_EQ(idle.frames.size(), 2u);
    CHECK_EQ(walk.frames.size(), 1u);
    // delay is stored in ACT units (ms / 25).
    CHECK(idle.delay > 5.9f && idle.delay < 6.1f);   // 150 ms
    CHECK(walk.delay > 3.9f && walk.delay < 4.1f);   // 100 ms
    // Layers reference truecolor frames.
    CHECK_EQ(idle.frames[0].layers.size(), 1u);
    CHECK_EQ(idle.frames[0].layers[0].sprType, 1);
    CHECK_EQ(idle.frames[0].layers[0].sprIndex, 0);
    CHECK_EQ(walk.frames[0].layers[0].sprIndex, 1);
}

TEST_CASE(pngsprite_anchor_and_scale) {
    // scale 4: the 4x4 PNG is logically 1x1. Default anchor = bottom-centre of the LOGICAL
    // frame -> layer centre = (w/2 - w/2, h/2 - h) = (0, -h/2) in logical px.
    const std::string m =
        "scale 4\n"
        "action idle 150\n"
        "frame g.png\n";
    auto r = parsePngSprite(m, files({{"g.png", &kGreen4x4}}));
    CHECK(r.has_value());
    if (!r) return;
    CHECK(r->scale > 3.99f && r->scale < 4.01f);
    const auto& L = r->action.actions()[0].frames[0].layers[0];
    CHECK_EQ(L.x, 0);
    CHECK_EQ(L.y, -1);  // logical 1x1, feet anchor: h/2 - h = 0 - 1 (integer)
    // composeFrame size = frame_px * |scale| = 4 * 0.25 = 1 logical unit.
    CHECK(L.scaleX > 0.2499f && L.scaleX < 0.2501f);
    // Explicit anchor + per-frame offset land in the layer position.
    const std::string m2 =
        "anchor 0 0\n"
        "action idle 150\n"
        "frame g.png 2 -3\n";
    auto r2 = parsePngSprite(m2, files({{"g.png", &kGreen4x4}}));
    CHECK(r2.has_value());
    if (!r2) return;
    const auto& L2 = r2->action.actions()[0].frames[0].layers[0];
    CHECK_EQ(L2.x, 4);   // wLog/2 - 0 + dx = 2 + 2
    CHECK_EQ(L2.y, -1);  // hLog/2 - 0 + dy = 2 - 3
}

TEST_CASE(pngsprite_sound_events_and_gaps) {
    // attack (motion 2) with a sound event; motion 1 (walk) left undefined -> filled with
    // the first action so motion*8+dir never lands on an empty action.
    const std::string m =
        "action idle 150\n"
        "frame a.png\n"
        "action attack 80\n"
        "frame a.png sound hit.wav\n"
        "frame a.png\n";
    auto r = parsePngSprite(m, files({{"a.png", &kRed1x1}}));
    CHECK(r.has_value());
    if (!r) return;
    CHECK_EQ(r->action.actions().size(), 24u);  // motions 0..2
    CHECK_EQ(r->action.events().size(), 1u);
    CHECK_EQ(r->action.events()[0].name, std::string("hit.wav"));
    const auto& atk = r->action.actions()[2 * 8];
    CHECK_EQ(atk.frames[0].eventId, 0);
    CHECK_EQ(atk.frames[1].eventId, -1);
    // Gap motion 1 filled with idle's frames.
    CHECK_EQ(r->action.actions()[1 * 8].frames.size(), 1u);
}

TEST_CASE(pngsprite_errors) {
    std::string err;
    // Missing frame file.
    CHECK(!parsePngSprite("action idle 100\nframe nope.png\n", files({}), &err).has_value());
    CHECK(!err.empty());
    // Frame before any action.
    CHECK(!parsePngSprite("frame a.png\n", files({{"a.png", &kRed1x1}}), &err).has_value());
    // Unknown action name.
    CHECK(!parsePngSprite("action fly 100\nframe a.png\n", files({{"a.png", &kRed1x1}}), &err)
               .has_value());
    // Numeric action index IS accepted.
    CHECK(parsePngSprite("action 5 100\nframe a.png\n", files({{"a.png", &kRed1x1}}), &err)
              .has_value());
    // Empty manifest.
    CHECK(!parsePngSprite("", files({}), &err).has_value());
}

#include "world/SpriteComposer.hpp"

#include <vector>

#include "formats/Act.hpp"
#include "formats/Spr.hpp"
#include "microtest.hpp"

using namespace uaro;

namespace {

SprFrame solidFrame(u16 w, u16 h) {
    SprFrame f;
    f.width = w;
    f.height = h;
    f.pixels.assign(static_cast<usize>(w) * h * 4, 255);
    return f;
}

// One action, one frame, one layer drawing sprite frame `sprIndex` of kind `sprType`.
Action oneLayer(i32 sprIndex, i32 sprType) {
    ActLayer L;
    L.sprIndex = sprIndex;
    L.sprType = sprType;
    ActFrame F;
    F.layers.push_back(L);
    ActAction A;
    A.frames.push_back(F);
    return Action::fromParts({A}, {});
}

} // namespace

// A WebP-pack sprite with both kinds of frames: the .spr's indexed frames came out as <i>.webp and its
// truecolor frames as t<i>.webp; loadSprOrPngd appends the truecolor ones after the indexed ones and
// records where they start. A truecolor .act layer must land on that base, an indexed one on its own index.
TEST_CASE(compose_synthetic_truecolor_layer_uses_truecolor_base) {
    const Sprite spr =
        Sprite::fromRgbaFrames({solidFrame(2, 2), solidFrame(3, 3), solidFrame(5, 7)}, 1.0f, /*tcBase=*/2);
    std::vector<ComposedQuad> out;
    composeFrame(oneLayer(0, 1), spr, 0, 0, 0.0f, 0.0f, 0, out);  // truecolor frame 0 -> rgba frame 2
    CHECK_EQ(out.size(), static_cast<usize>(1));
    if (out.empty()) return;
    CHECK_EQ(out[0].sprIndex, 2);
    CHECK(!out[0].indexed);
    CHECK(out[0].w > 4.99f && out[0].w < 5.01f);  // the 5x7 frame, not frame 0 (2x2)
    out.clear();
    composeFrame(oneLayer(1, 0), spr, 0, 0, 0.0f, 0.0f, 0, out);  // indexed frame 1 -> rgba frame 1
    CHECK_EQ(out.size(), static_cast<usize>(1));
    if (!out.empty()) CHECK_EQ(out[0].sprIndex, 1);
}

// fireball/waterball/particle1: pure truecolor sprites, only t<i> frames -> base 0, indexes map 1:1.
TEST_CASE(compose_synthetic_pure_truecolor_sprite) {
    const Sprite spr = Sprite::fromRgbaFrames({solidFrame(4, 4), solidFrame(6, 6)}, 1.0f, 0);
    std::vector<ComposedQuad> out;
    composeFrame(oneLayer(1, 1), spr, 0, 0, 0.0f, 0.0f, 0, out);
    CHECK_EQ(out.size(), static_cast<usize>(1));
    if (!out.empty()) CHECK_EQ(out[0].sprIndex, 1);
}

// sprIndex -1 is the .act's "empty layer". With a truecolor sprType it must stay empty, not become
// -1 + base = the last indexed frame.
TEST_CASE(compose_synthetic_empty_truecolor_layer_draws_nothing) {
    const Sprite spr =
        Sprite::fromRgbaFrames({solidFrame(2, 2), solidFrame(3, 3), solidFrame(5, 7)}, 1.0f, /*tcBase=*/2);
    std::vector<ComposedQuad> out;
    composeFrame(oneLayer(-1, 1), spr, 0, 0, 0.0f, 0.0f, 0, out);
    CHECK_EQ(out.size(), static_cast<usize>(0));
}

// A truecolor layer whose frame the pack does not have (base past the end) draws nothing instead of
// borrowing an unrelated indexed frame.
TEST_CASE(compose_synthetic_missing_truecolor_frame_is_skipped) {
    const Sprite spr = Sprite::fromRgbaFrames({solidFrame(2, 2)}, 1.0f, /*tcBase=*/1);
    std::vector<ComposedQuad> out;
    composeFrame(oneLayer(0, 1), spr, 0, 0, 0.0f, 0.0f, 0, out);
    CHECK_EQ(out.size(), static_cast<usize>(0));
}

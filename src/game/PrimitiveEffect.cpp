#include "game/PrimitiveEffect.hpp"

#include <algorithm>
#include <cmath>

#include <bgfx/bgfx.h>

#include "core/Log.hpp"
#include "formats/ImageIO.hpp"      // decodeImage + keyAndDespillMagenta (same path as StrEffect layers)
#include "game/CharacterActor.hpp"  // WorldSpritePass
#include "resource/Vfs.hpp"

namespace uaro {
namespace {

constexpr float kFps = 60.0f;
constexpr unsigned kTotalFrames = 300;  // 5 s at 60 fps, matching the original
constexpr float kPi = 3.14159265358979323846f;

// RO-native effect units -> our world units. nostalre's seed numbers (distance ~3, max_height 8,
// ribbon heights 1.5-3) are in the original game's small world; our world (cellToWorld: 1 GND cell =
// 1 unit, a character ~2 units tall) is a different scale, so convert. Starting value tuned so the
// funnel reads as a tornado sitting just above the caster's head; exposed here for visual tuning.
constexpr float kUnit = 0.35f;

// wind.rs per-slot seed: (arc deg, distance, rot_start deg, ribbon height).
constexpr float kSeedArcDeg[WindEffect::kSlots] = {90.0f, 70.0f, 50.0f, 30.0f};
constexpr float kSeedDistance[WindEffect::kSlots] = {3.0f, 3.2f, 3.4f, 3.6f};
constexpr float kSeedRotDeg[WindEffect::kSlots] = {0.0f, 10.0f, 20.0f, 30.0f};
constexpr float kSeedHeight[WindEffect::kSlots] = {3.0f, 2.5f, 2.0f, 1.5f};

constexpr float kMaxHeight = 8.0f;  // ribbon base elevation above the caster's feet (RO-native units)
constexpr float kHeightScale = 1.0f;

constexpr float kRotSpinPerFrameDeg = 5.0f;
constexpr float kArcGrowthPerFrameDeg = 3.0f;
constexpr float kArcMaxDeg = 120.0f;

constexpr unsigned kAlphaRampFrames = 12;
constexpr float kAlphaRampStep = 10.0f / 255.0f;
constexpr float kAlphaCap = 250.0f / 255.0f;

struct V {
    f32 x, y, z, u, v;
    u32 abgr;
};

inline float radf(float deg) { return deg * kPi / 180.0f; }

}  // namespace

WindEffect::WindEffect(const Vec3& worldPos, double bornTime) : born_(bornTime) {
    (void)worldPos;  // the funnel follows the caster's live pos passed to render(); kept for clarity
    for (int i = 0; i < kSlots; ++i) {
        Slot& s = slots_[i];
        s.alive = true;
        s.process = 0;
        s.distance = kSeedDistance[i];
        s.fullArcDeg = kSeedArcDeg[i];
        s.rotStartDeg = kSeedRotDeg[i];
        s.alphaB = 0.0f;  // ramps up over the first 12 frames
        for (float& h : s.height) h = kSeedHeight[i];
    }
}

void WindEffect::integrateTo(unsigned targetFrame) {
    while (lastFrame_ < targetFrame) {
        for (Slot& s : slots_) {
            if (!s.alive) continue;
            s.process += 1;
            s.rotStartDeg += kRotSpinPerFrameDeg;
            if (s.rotStartDeg >= 360.0f) s.rotStartDeg -= 360.0f;
            s.fullArcDeg = std::min(s.fullArcDeg + kArcGrowthPerFrameDeg, kArcMaxDeg);
            // nostalre ticks process (+1) then checks `process < 12`, so the alpha ramps on the first
            // 11 integrated frames (its doc rounds this to "12 frames ~= 120/255"); match the code exactly.
            if (s.process < kAlphaRampFrames) s.alphaB = std::min(s.alphaB + kAlphaRampStep, kAlphaCap);
        }
        ++lastFrame_;
    }
}

void WindEffect::advance(double now) {
    ageFrames_ = static_cast<float>((now - born_) * kFps);
    unsigned target = static_cast<unsigned>(std::max(0.0f, ageFrames_));
    if (target > kTotalFrames) target = kTotalFrames;
    integrateTo(target);
}

bool WindEffect::finished(double now) const {
    return (now - born_) * kFps >= static_cast<double>(kTotalFrames);
}

void WindEffect::render(const WorldSpritePass& pass, const Texture& tex, const Vec3& base) const {
    if (!bgfx::isValid(pass.program) || !pass.layout || !tex.valid()) return;

    // Ribbon base sits kMaxHeight above the caster's feet. Our world is +Y up (native RO is -Y up,
    // so where nostalre subtracts in Y we add). rise_angle 90deg -> ribbons stand straight up.
    const Vec3 center{base.x, base.y + kMaxHeight * kUnit, base.z};
    constexpr int segments = kDivision - 1;  // 20

    for (const Slot& s : slots_) {
        if (!s.alive || s.alphaB <= 0.0f) continue;

        const float arc = radf(s.fullArcDeg);
        const float rot = radf(s.rotStartDeg);

        // Build the ring positions (bottom on the circle, top lifted straight up by height).
        Vec3 bottom[segments + 1];
        Vec3 top[segments + 1];
        for (int i = 0; i <= segments; ++i) {
            const float frac = static_cast<float>(i) / static_cast<float>(segments);
            const float theta = rot + frac * arc;
            const float ct = std::cos(theta), st = std::sin(theta);
            const float h = s.height[i % kDivision] * kHeightScale * kUnit;  // rise_angle 90: all upward
            bottom[i] = Vec3{center.x + s.distance * kUnit * ct, center.y, center.z + s.distance * kUnit * st};
            top[i] = Vec3{bottom[i].x, bottom[i].y + h, bottom[i].z};
        }

        // Stitch adjacent bottom/top pairs into a quad strip (U runs around the arc, V bottom->top).
        const auto cb = [](float f) { return static_cast<u32>(std::clamp(f, 0.0f, 1.0f) * 255.0f + 0.5f); };
        const u32 a = cb(s.alphaB);
        const u32 col = (a << 24) | 0x00ffffffu;  // ABGR, white RGB, per-slot alpha

        if (bgfx::getAvailTransientVertexBuffer(segments * 4, *pass.layout) < static_cast<u32>(segments * 4) ||
            bgfx::getAvailTransientIndexBuffer(segments * 6) < static_cast<u32>(segments * 6))
            return;
        bgfx::TransientVertexBuffer tvb;
        bgfx::TransientIndexBuffer tib;
        bgfx::allocTransientVertexBuffer(&tvb, segments * 4, *pass.layout);
        bgfx::allocTransientIndexBuffer(&tib, segments * 6);
        V* vd = reinterpret_cast<V*>(tvb.data);
        u16* id = reinterpret_cast<u16*>(tib.data);
        int vn = 0, in = 0;
        for (int seg = 0; seg < segments; ++seg) {
            const float u0 = static_cast<float>(seg) / static_cast<float>(segments);
            const float u1 = static_cast<float>(seg + 1) / static_cast<float>(segments);
            const u16 b = static_cast<u16>(vn);
            vd[vn++] = {bottom[seg].x, bottom[seg].y, bottom[seg].z, u0, 1.0f, col};
            vd[vn++] = {bottom[seg + 1].x, bottom[seg + 1].y, bottom[seg + 1].z, u1, 1.0f, col};
            vd[vn++] = {top[seg + 1].x, top[seg + 1].y, top[seg + 1].z, u1, 0.0f, col};
            vd[vn++] = {top[seg].x, top[seg].y, top[seg].z, u0, 0.0f, col};
            id[in++] = b;     id[in++] = b + 1; id[in++] = b + 2;
            id[in++] = b;     id[in++] = b + 2; id[in++] = b + 3;
        }

        bgfx::setVertexBuffer(0, &tvb);
        bgfx::setIndexBuffer(&tib);
        bgfx::setTexture(0, pass.sampler, tex.handle());
        if (bgfx::isValid(pass.bias)) {
            const float z[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            bgfx::setUniform(pass.bias, z);  // no per-layer depth bias for effects
        }
        if (bgfx::isValid(pass.fade)) {
            const float f[4] = {1.0f, 0.0f, 0.0f, 0.0f};
            bgfx::setUniform(pass.fade, f);  // shader alpha = fade * vertex.a -> use vertex alpha
        }
        // Alpha blend (nostalre BlendKind::Alpha), depth-test so map/units occlude, no Z-write, cull off.
        const u64 state = BGFX_STATE_WRITE_RGB | BGFX_STATE_DEPTH_TEST_LEQUAL |
                          BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);
        bgfx::setState(state);
        bgfx::submit(pass.view, pass.program);
    }
}

bool loadEffectTexture(const Vfs& vfs, const char* nameTga, Texture& out) {
    const std::string path = std::string("data/texture/effect/") + nameTga;
    auto bytes = vfs.readPreferPng(path);
    if (!bytes) {
        log::warn("PrimitiveEffect: texture NOT FOUND at '{}' (check the content pack)", path);
        return false;
    }
    auto img = decodeImage(*bytes);
    if (!img || !img->valid()) {
        log::warn("PrimitiveEffect: '{}' loaded ({} bytes) but decode failed", path, bytes->size());
        return false;
    }
    keyAndDespillMagenta(*img);  // key the magenta background + despill the rim (shared with StrEffect)
    out.create(static_cast<u16>(img->width), static_cast<u16>(img->height), img->rgba.data(), /*smooth=*/true);
    return out.valid();
}

}  // namespace uaro

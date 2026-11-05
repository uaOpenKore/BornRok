#include "game/PrimitiveEffect.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

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

inline u32 abgr(float r, float g, float b, float a) {
    const auto cb = [](float f) { return static_cast<u32>(std::clamp(f, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return (cb(a) << 24) | (cb(b) << 16) | (cb(g) << 8) | cb(r);
}

// Set the per-effect bias/fade uniforms (bias 0, fade 1 -> shader uses vertex alpha), exactly as
// StrEffect does, then submit one textured primitive. `additive` picks the blend; `depthTest` keeps
// the LEQUAL depth test (closer geometry occludes) -- off for ground-coplanar quads that the terrain
// they rest on would otherwise hide.
void submitPrim(const WorldSpritePass& pass, const Texture& tex, const V* verts, int vn,
                const u16* idx, int in, bool additive, bool depthTest) {
    if (bgfx::getAvailTransientVertexBuffer(vn, *pass.layout) < static_cast<u32>(vn) ||
        bgfx::getAvailTransientIndexBuffer(in) < static_cast<u32>(in))
        return;
    bgfx::TransientVertexBuffer tvb;
    bgfx::TransientIndexBuffer tib;
    bgfx::allocTransientVertexBuffer(&tvb, vn, *pass.layout);
    bgfx::allocTransientIndexBuffer(&tib, in);
    std::memcpy(tvb.data, verts, sizeof(V) * static_cast<usize>(vn));
    std::memcpy(tib.data, idx, sizeof(u16) * static_cast<usize>(in));
    bgfx::setVertexBuffer(0, &tvb);
    bgfx::setIndexBuffer(&tib);
    bgfx::setTexture(0, pass.sampler, tex.handle());
    if (bgfx::isValid(pass.bias)) {
        const float z[4] = {0, 0, 0, 0};
        bgfx::setUniform(pass.bias, z);
    }
    if (bgfx::isValid(pass.fade)) {
        const float f[4] = {1, 0, 0, 0};
        bgfx::setUniform(pass.fade, f);
    }
    u64 state = BGFX_STATE_WRITE_RGB;
    if (depthTest) state |= BGFX_STATE_DEPTH_TEST_LEQUAL;
    state |= additive ? (BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE))
                      : (BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA));
    bgfx::setState(state);
    bgfx::submit(pass.view, pass.program);
}

// A camera-facing textured quad centred at `center`, half-extents (hw, hh) along the billboard axes,
// rolled by `angle` rad in the screen plane (R=pass.right, U=pass.up). Full-texture UV.
void drawBillboardQuad(const WorldSpritePass& pass, const Texture& tex, const Vec3& center, float hw,
                       float hh, float angle, u32 col, bool additive) {
    const float cs = std::cos(angle), sn = std::sin(angle);
    const float lx[4] = {-hw, hw, hw, -hw}, ly[4] = {-hh, -hh, hh, hh};
    const float u[4] = {0, 1, 1, 0}, vv[4] = {1, 1, 0, 0};
    V vd[4];
    for (int c = 0; c < 4; ++c) {
        const float rx = lx[c] * cs - ly[c] * sn;
        const float ry = lx[c] * sn + ly[c] * cs;
        const Vec3 p{center.x + pass.right.x * rx + pass.up.x * ry,
                     center.y + pass.right.y * rx + pass.up.y * ry,
                     center.z + pass.right.z * rx + pass.up.z * ry};
        vd[c] = {p.x, p.y, p.z, u[c], vv[c], col};
    }
    const u16 id[6] = {0, 1, 2, 0, 2, 3};
    submitPrim(pass, tex, vd, 4, id, 6, additive, /*depthTest=*/true);
}

// A camera-facing textured disc (triangle fan) of `radius`, `segments` around, UV centred at 0.5.
void drawBillboardDisc(const WorldSpritePass& pass, const Texture& tex, const Vec3& center,
                       float radius, int segments, u32 col, bool additive) {
    const int n = segments + 2;  // centre + (segments+1) rim
    std::vector<V> vd(static_cast<usize>(n));
    std::vector<u16> id(static_cast<usize>(segments) * 3);
    vd[0] = {center.x, center.y, center.z, 0.5f, 0.5f, col};
    for (int i = 0; i <= segments; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(segments) * 2.0f * kPi;
        const float ct = std::cos(t), st = std::sin(t);
        const Vec3 p{center.x + pass.right.x * radius * ct + pass.up.x * radius * st,
                     center.y + pass.right.y * radius * ct + pass.up.y * radius * st,
                     center.z + pass.right.z * radius * ct + pass.up.z * radius * st};
        vd[static_cast<usize>(i + 1)] = {p.x, p.y, p.z, 0.5f + 0.5f * ct, 0.5f + 0.5f * st, col};
    }
    for (int i = 0; i < segments; ++i) {
        id[static_cast<usize>(i) * 3 + 0] = 0;
        id[static_cast<usize>(i) * 3 + 1] = static_cast<u16>(i + 1);
        id[static_cast<usize>(i) * 3 + 2] = static_cast<u16>(i + 2);
    }
    submitPrim(pass, tex, vd.data(), n, id.data(), segments * 3, additive, /*depthTest=*/true);
}

// A flat textured quad on the world XZ plane (ground ring). `no_depth` -> no depth test so the
// terrain it rests on cannot occlude it.
void drawFlatQuad(const WorldSpritePass& pass, const Texture& tex, const Vec3& c, float half, u32 col) {
    V vd[4] = {
        {c.x - half, c.y, c.z - half, 0, 0, col},
        {c.x + half, c.y, c.z - half, 1, 0, col},
        {c.x + half, c.y, c.z + half, 1, 1, col},
        {c.x - half, c.y, c.z + half, 0, 1, col},
    };
    const u16 id[6] = {0, 1, 2, 0, 2, 3};
    submitPrim(pass, tex, vd, 4, id, 6, /*additive=*/false, /*depthTest=*/false);
}

// Linear fade-in to `peak` over `fadeIn` frames, hold, linear fade-out from `fadeOutStart` to `total`.
float fadeInOut(float frame, float peak, float fadeIn, float fadeOutStart, float total) {
    const float rise = std::clamp(frame / std::max(fadeIn, 1e-3f), 0.0f, 1.0f);
    float fall = 1.0f;
    if (frame >= fadeOutStart) {
        const float span = std::max(total - fadeOutStart, 1e-3f);
        fall = std::clamp(1.0f - (frame - fadeOutStart) / span, 0.0f, 1.0f);
    }
    return peak * rise * fall;
}

// ---- Turn Undead (EF_TURNUNDEAD 82) constants, ported from nostalre turnundead.rs + spike_burst.rs
constexpr float kTuPhase1Frames = 20.0f;
constexpr float kTuHaloRadius = 10.0f;
constexpr float kTuHaloMaxAlpha = 130.0f / 255.0f;
constexpr float kTuHaloFadeIn = 6.0f;
constexpr float kTuHaloFadeOutAt = 10.0f;
constexpr float kTuHaloHeight = -5.0f;  // RO-native -Y up -> our +Y: add -height*kUnit

constexpr int kTuSpikeCount = 5;
constexpr float kTuSpikeDuration = 20.0f;
constexpr float kTuSpikeThickness = 0.3f;
constexpr float kTuSpikeMaxAlpha = 200.0f / 255.0f;
constexpr float kTuSpikeFadeIn = 10.0f;
constexpr float kTuSpikeFadeOutAt = kTuSpikeDuration - kTuSpikeDuration / 3.0f;  // ~13.33

constexpr float kTuRingSpawn = 50.0f;
constexpr float kTuRingDuration = 30.0f;
constexpr float kTuRingFadeOutAt = 24.0f;
constexpr float kTuRingHeight = -2.0f;
constexpr float kTuRingScale = 0.8f;
constexpr float kTuRingChangeFrame = 15.0f;
constexpr float kTuTotalFrames = kTuRingSpawn + kTuRingDuration;  // 80

float tuRingHalfExtent(float frame) {
    float raw;
    if (frame <= kTuRingChangeFrame) {
        raw = 1.5f * frame + 0.075f * frame * (frame + 1.0f) / 2.0f;
    } else {
        const float s15 = 1.5f * kTuRingChangeFrame +
                          0.075f * kTuRingChangeFrame * (kTuRingChangeFrame + 1.0f) / 2.0f;
        const float g = frame - kTuRingChangeFrame;
        raw = s15 + 0.5f * g - (0.5f / 15.0f) * g * (g + 1.0f) / 2.0f;
    }
    return raw * kTuRingScale;
}

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

TurnUndeadEffect::TurnUndeadEffect(const Vec3& worldPos, double bornTime) : born_(bornTime) {
    // Deterministic per-position spike layout (nostalre seed_from_world + LCG).
    auto bits = [](float f) { u32 u; std::memcpy(&u, &f, 4); return u; };
    auto rotl = [](u32 v, int s) { return (v << s) | (v >> (32 - s)); };
    u32 state = (bits(worldPos.x) ^ rotl(bits(worldPos.z), 13)) ^ 0x9E3779B9u;
    auto lcg = [&]() {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(state >> 8) / static_cast<float>(1u << 24);
    };
    for (int i = 0; i < kSpikes; ++i) {
        const float lonDeg = lcg() * 360.0f;
        const float angDeg = 1.0f + lcg() * (7.0f - 1.0f);        // angular_speed_deg_range (1,7)
        const float lenInit = 9.0f + lcg() * (15.0f - 9.0f);      // length_init_range (9,15)
        const float growth = -0.3f + lcg() * (-0.2f - (-0.3f));   // growth_range (-0.3,-0.2)
        spikes_[i].longitude0 = radf(lonDeg);
        spikes_[i].angSpeed = -radf(angDeg);  // sign-flip to screen CCW (nostalre convention)
        spikes_[i].lenInit = lenInit;
        spikes_[i].growth = growth;
    }
}

void TurnUndeadEffect::advance(double now) {
    ageFrames_ = static_cast<float>(std::max(0.0, (now - born_) * kFps));
}

bool TurnUndeadEffect::finished(double now) const {
    return (now - born_) * kFps >= static_cast<double>(kTuTotalFrames);
}

void TurnUndeadEffect::render(const WorldSpritePass& pass, const Texture& halo, const Texture& spike,
                              const Texture& ring, const Vec3& base) const {
    if (!bgfx::isValid(pass.program) || !pass.layout) return;
    const float f = ageFrames_;

    // Phase 1 -- shrinking halo disc (alpha-blended, -Y-up offset -> our +Y).
    if (f < kTuPhase1Frames && halo.valid()) {
        const float a = fadeInOut(f, kTuHaloMaxAlpha, kTuHaloFadeIn, kTuHaloFadeOutAt, kTuPhase1Frames);
        const float radius = kTuHaloRadius * (1.0f - f / kTuPhase1Frames);
        if (a > 0.0f && radius > 0.0f) {
            const Vec3 c{base.x, base.y - kTuHaloHeight * kUnit, base.z};
            drawBillboardDisc(pass, halo, c, radius * kUnit, 32, abgr(0.6f, 0.7f, 1.0f, a), /*additive=*/false);
        }
    }

    // Phase 1 -- 5 shrinking additive flash rays straddling the centre.
    if (f < kTuSpikeDuration && spike.valid()) {
        const float a = fadeInOut(f, kTuSpikeMaxAlpha, kTuSpikeFadeIn, kTuSpikeFadeOutAt, kTuSpikeDuration);
        if (a > 0.0f) {
            const Vec3 c{base.x, base.y - kTuHaloHeight * kUnit, base.z};
            for (const Spike& s : spikes_) {
                // longitude(frame) with decel accel = -angSpeed/duration/1.5 (nostalre Spike::longitude)
                const float accel = -s.angSpeed / kTuSpikeDuration / 1.5f;
                const float lon = s.longitude0 + s.angSpeed * f + accel * f * (f + 1.0f) / 2.0f;
                const float length = s.lenInit + s.growth * f;
                if (length <= 0.0f) continue;
                drawBillboardQuad(pass, spike, c, kTuSpikeThickness * 0.5f * kUnit, length * kUnit, lon,
                                  abgr(1.0f, 1.0f, 1.0f, a), /*additive=*/true);
            }
        }
    }

    // Phase 2 -- expanding flat ground ring (alpha, no depth).
    const float ringFrame = f - kTuRingSpawn;
    if (ringFrame >= 0.0f && ringFrame < kTuRingDuration && ring.valid()) {
        const float half = tuRingHalfExtent(ringFrame);
        const float a = fadeInOut(ringFrame, 1.0f, 1.0f, kTuRingFadeOutAt, kTuRingDuration);
        if (half > 0.0f && a > 0.0f) {
            const Vec3 c{base.x, base.y - kTuRingHeight * kUnit, base.z};
            drawFlatQuad(pass, ring, c, half * kUnit, abgr(1.0f, 1.0f, 1.0f, a));
        }
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

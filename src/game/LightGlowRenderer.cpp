#include "game/LightGlowRenderer.hpp"

#include <algorithm>
#include <cstdint>

#include "app/Application.hpp"
#include "core/Log.hpp"
#include "render/Shader.hpp"

namespace uaro {

namespace {
// Master glow intensity (baked into vertex alpha) — additive, so keep modest to avoid blow-out
// when several lights overlap. Tunable to taste.
constexpr float kGlowIntensity = 1.8f;  // ×2 per S. — punchier warm halos; still additive/clamped
struct GlowVert {
    f32 x, y, z, u, v;
    u32 abgr;
};
u8 cb(float c01) {
    return static_cast<u8>(std::clamp(c01, 0.0f, 1.0f) * 255.0f + 0.5f);
}
}  // namespace

bool LightGlowRenderer::load(Application& app) {
    // Reload the GPU program ONLY — must NOT clear lights_. spawnMapEffects() calls setLights()
    // BEFORE load() on the first map (log: "156 glow(s)" prints before "vs_glow ok"), so the old
    // destroy() here wiped the 156 lights right after they were set -> lights_.empty() at render ->
    // no halos ever. Lights are map data; the program is a GPU resource. Keep them separate.
    if (bgfx::isValid(program_)) bgfx::destroy(program_);
    program_ = BGFX_INVALID_HANDLE;
    ready_ = false;
    program_ = load_program(app.assetDir(), "vs_glow", "fs_glow");
    if (!bgfx::isValid(program_)) {
        log::warn("LightGlowRenderer: glow shader unavailable -> dungeon light glows off");
        return false;
    }
    layout_.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();
    ready_ = true;
    return true;
}

void LightGlowRenderer::destroy() {
    if (bgfx::isValid(program_)) bgfx::destroy(program_);
    program_ = BGFX_INVALID_HANDLE;
    lights_.clear();
    ready_ = false;
}

void LightGlowRenderer::render(bgfx::ViewId view, const Vec3& right, const Vec3& up,
                              const Glow* extras, u32 extraCount) const {
    if (!ready_ || !bgfx::isValid(program_)) return;
    const u32 nl = static_cast<u32>(lights_.size());
    const u32 n = nl + extraCount;  // + the optional extra glows (sun disc layers)
    if (n == 0) return;
    if (bgfx::getAvailTransientVertexBuffer(n * 4, layout_) < n * 4 ||
        bgfx::getAvailTransientIndexBuffer(n * 6) < n * 6)
        return;
    bgfx::TransientVertexBuffer tvb;
    bgfx::TransientIndexBuffer tib;
    bgfx::allocTransientVertexBuffer(&tvb, n * 4, layout_);
    bgfx::allocTransientIndexBuffer(&tib, n * 6);
    GlowVert* vd = reinterpret_cast<GlowVert*>(tvb.data);
    u16* id = reinterpret_cast<u16*>(tib.data);

    for (u32 i = 0; i < n; ++i) {
        const Glow& L = (i >= nl) ? extras[i - nl] : lights_[i];
        const float s = L.halfSize;
        const Vec3 R = right * s, U = up * s;
        // abgr = 0xAA'BB'GG'RR (RGBA in memory); alpha carries the master intensity.
        const u32 col = (cb(kGlowIntensity) << 24) | (cb(L.b) << 16) | (cb(L.g) << 8) | cb(L.r);
        const Vec3 c0 = L.pos + R * -1.0f + U * -1.0f;
        const Vec3 c1 = L.pos + R * 1.0f + U * -1.0f;
        const Vec3 c2 = L.pos + R * 1.0f + U * 1.0f;
        const Vec3 c3 = L.pos + R * -1.0f + U * 1.0f;
        GlowVert* q = &vd[i * 4];
        q[0] = {c0.x, c0.y, c0.z, 0.0f, 0.0f, col};
        q[1] = {c1.x, c1.y, c1.z, 1.0f, 0.0f, col};
        q[2] = {c2.x, c2.y, c2.z, 1.0f, 1.0f, col};
        q[3] = {c3.x, c3.y, c3.z, 0.0f, 1.0f, col};
        const u16 b = static_cast<u16>(i * 4);
        u16* t = &id[i * 6];
        t[0] = b; t[1] = static_cast<u16>(b + 1); t[2] = static_cast<u16>(b + 2);
        t[3] = b; t[4] = static_cast<u16>(b + 2); t[5] = static_cast<u16>(b + 3);
    }

    bgfx::setVertexBuffer(0, &tvb);
    bgfx::setIndexBuffer(&tib);
    // Additive overlay, NO depth test: the glows layer over the whole scene like the .str effects
    // (magnum etc.). Depth-testing them (the old behaviour) hid every glow that a raised sewer
    // platform/wall edge occluded from the low camera — which was most of them, so toggling Glow
    // did nothing on screen. A soft light haze reading slightly through a near wall is the RO look
    // anyway; correctness beats a per-quad-centre occlusion that erased the whole effect.
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_BLEND_ADD);
    bgfx::submit(view, program_);
}

}  // namespace uaro

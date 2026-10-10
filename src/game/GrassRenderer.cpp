#include "game/GrassRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "app/Application.hpp"
#include "core/Log.hpp"
#include "render/SamplerFilter.hpp"  // g_grassEnabled / g_grassHeightFrac
#include "render/Shader.hpp"
#include "world/MapData.hpp"

namespace uaro {

namespace {
struct GVertex {
    f32 x, y, z, u, v;
    u32 abgr;
};
// Grass height is a fraction of this (one world unit ~ one map cell ~ a character's height). The
// Normals level scales it: x1 = 0.20, x1.5 = 0.30, x2 = 0.40 (S.).
constexpr float kCharHeight = 1.0f;
constexpr u32 kMaxClumps = 40000;  // cap the scatter so a huge field can't flood the frame pool

// Deterministic per-cell jitter in [0,1) (no RNG -> identical every load).
inline float hash01(u32 a, u32 b, u32 c) {
    u32 h = a * 73856093u ^ b * 19349663u ^ c * 83492791u;
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return static_cast<float>(h & 0xffffffu) / static_cast<float>(0x1000000u);
}

// A small procedural grass-tuft sprite: several green blades on transparent background, tips at the
// top (row 0), darker at the base. Alpha-cutout keyed by the blade silhouette.
bgfx::TextureHandle makeGrassTexture() {
    constexpr int W = 24, H = 32;
    const float peakX[5] = {0.12f, 0.30f, 0.50f, 0.68f, 0.86f};
    const float peakH[5] = {0.72f, 1.00f, 0.88f, 1.00f, 0.68f};
    const float hwFrac = 0.11f;  // blade half-width as a fraction of W
    std::vector<u8> px(static_cast<usize>(W) * H * 4, 0);
    for (int x = 0; x < W; ++x) {
        const float xf = static_cast<float>(x) / (W - 1);
        float topH = 0.0f;
        for (int p = 0; p < 5; ++p) {
            const float d = std::fabs(xf - peakX[p]);
            if (d < hwFrac) topH = std::max(topH, (1.0f - d / hwFrac) * peakH[p] * H);
        }
        for (int y = 0; y < H; ++y) {
            const float yb = static_cast<float>(H - 1 - y);  // height from the bottom
            u8* o = &px[(static_cast<usize>(y) * W + x) * 4];
            if (topH < 1.0f || yb > topH) { o[3] = 0; continue; }  // gap / above the blade tip
            const float t = std::clamp(yb / std::max(topH, 1.0f), 0.0f, 1.0f);  // 0 base -> 1 tip
            o[0] = static_cast<u8>(34 + (130 - 34) * t);    // r
            o[1] = static_cast<u8>(80 + (185 - 80) * t);    // g
            o[2] = static_cast<u8>(24 + (70 - 24) * t);     // b
            o[3] = 255;
        }
    }
    return bgfx::createTexture2D(static_cast<u16>(W), static_cast<u16>(H), false, 1,
                                 bgfx::TextureFormat::RGBA8,
                                 BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
                                 bgfx::copy(px.data(), static_cast<u32>(px.size())));
}

// Average colour of a decoded texture (sampled sparsely) -> is it "grassy" (green-dominant)?
bool isGrassy(const Image& img) {
    if (!img.valid()) return false;
    u64 r = 0, g = 0, b = 0, n = 0;
    const usize step = std::max<usize>(1, (static_cast<usize>(img.width) * img.height) / 4096);
    for (usize i = 0; i < static_cast<usize>(img.width) * img.height; i += step) {
        const u8* p = &img.rgba[i * 4];
        r += p[0]; g += p[1]; b += p[2]; ++n;
    }
    if (n == 0) return false;
    const float ar = static_cast<float>(r) / n, ag = static_cast<float>(g) / n, ab = static_cast<float>(b) / n;
    // Green must clearly lead red and blue (fields), and not be near-black. Dirt/stone are r>=g or grey.
    return ag > 45.0f && ag > ar * 1.08f && ag - ar > 10.0f && ag - ab > 18.0f;
}
}  // namespace

bool GrassRenderer::load(Application& app, const MapData& map) {
    program_ = load_program(app.assetDir(), "vs_sprite3d", "fs_sprite3d");
    if (!bgfx::isValid(program_)) {
        log::warn("GrassRenderer: sprite shader unavailable; no grass");
        return false;
    }
    const Gnd& g = map.gnd;
    const u32 W = g.width(), H = g.height();
    const auto& cubes = g.cubes();
    const auto& surfs = g.surfaces();
    if (W == 0 || H == 0 || cubes.size() < static_cast<usize>(W) * H) {
        bgfx::destroy(program_); program_ = BGFX_INVALID_HANDLE; return false;
    }

    // Which ground textures are grass (green-dominant)?
    std::vector<bool> grassy(map.textures.size(), false);
    u32 grassyTex = 0;
    for (usize ti = 0; ti < map.textures.size(); ++ti)
        if (map.textures[ti] && isGrassy(*map.textures[ti])) { grassy[ti] = true; ++grassyTex; }
    if (grassyTex == 0) {  // no grass on this map -> nothing to do (indoor/desert/etc.)
        bgfx::destroy(program_); program_ = BGFX_INVALID_HANDLE; return false;
    }

    // One tuft per grass cell (jittered within the cell), up to the cap.
    for (u32 y = 0; y < H && clumps_.size() < kMaxClumps; ++y) {
        for (u32 x = 0; x < W && clumps_.size() < kMaxClumps; ++x) {
            const GndCube& c = cubes[x + y * W];
            if (c.tileUp < 0 || static_cast<usize>(c.tileUp) >= surfs.size()) continue;
            const int tid = surfs[c.tileUp].textureId;
            if (tid < 0 || static_cast<usize>(tid) >= grassy.size() || !grassy[tid]) continue;
            const float jx = 0.2f + 0.6f * hash01(x, y, 1);  // keep tufts off the exact cell edge
            const float jz = 0.2f + 0.6f * hash01(x, y, 2);
            // Bilinear corner height (same mapping as MapRenderer::heightAt), world Y = -h*0.1.
            const float top = c.height[0] + (c.height[1] - c.height[0]) * jx;
            const float bot = c.height[2] + (c.height[3] - c.height[2]) * jx;
            const float gy = -(top + (bot - top) * jz) * 0.1f;
            const float wx = static_cast<float>(W) - (static_cast<float>(x) + jx);  // X mirror (world = W - cell)
            const float wz = static_cast<float>(y) + jz;
            clumps_.push_back({wx, wz, gy, 0xffffffffu});
        }
    }
    if (clumps_.empty()) { bgfx::destroy(program_); program_ = BGFX_INVALID_HANDLE; return false; }

    grassTex_ = makeGrassTexture();
    sampler_ = bgfx::createUniform("s_tex", bgfx::UniformType::Sampler);
    fade_ = bgfx::createUniform("u_spriteFade", bgfx::UniformType::Vec4);
    bias_ = bgfx::createUniform("u_spriteBias", bgfx::UniformType::Vec4);
    ready_ = true;
    buildMesh(g_grassHeightFrac);
    log::info("GrassRenderer: {} tufts on {} grassy textures ({}x{}), height frac {}",
              clumps_.size(), grassyTex, W, H, g_grassHeightFrac);
    return true;
}

void GrassRenderer::buildMesh(float heightFrac) {
    if (!ready_ || clumps_.empty()) { quadCount_ = 0; return; }
    const float h = kCharHeight * std::clamp(heightFrac, 0.05f, 1.0f);
    const float hw = h * 0.45f;  // tuft half-width
    std::vector<GVertex> verts;
    std::vector<u32> idx;
    verts.reserve(clumps_.size() * 8);
    idx.reserve(clumps_.size() * 12);
    for (const Clump& c : clumps_) {
        const float by = c.groundY, ty = c.groundY + h;
        const u32 col = c.abgr;
        // Quad A: spans X at the clump's Z (faces +/-Z).
        u32 base = static_cast<u32>(verts.size());
        verts.push_back({c.x - hw, by, c.z, 0.0f, 1.0f, col});
        verts.push_back({c.x + hw, by, c.z, 1.0f, 1.0f, col});
        verts.push_back({c.x + hw, ty, c.z, 1.0f, 0.0f, col});
        verts.push_back({c.x - hw, ty, c.z, 0.0f, 0.0f, col});
        idx.push_back(base + 0); idx.push_back(base + 1); idx.push_back(base + 2);
        idx.push_back(base + 0); idx.push_back(base + 2); idx.push_back(base + 3);
        // Quad B: spans Z at the clump's X (faces +/-X) — the perpendicular half of the cross.
        base = static_cast<u32>(verts.size());
        verts.push_back({c.x, by, c.z - hw, 0.0f, 1.0f, col});
        verts.push_back({c.x, by, c.z + hw, 1.0f, 1.0f, col});
        verts.push_back({c.x, ty, c.z + hw, 1.0f, 0.0f, col});
        verts.push_back({c.x, ty, c.z - hw, 0.0f, 0.0f, col});
        idx.push_back(base + 0); idx.push_back(base + 1); idx.push_back(base + 2);
        idx.push_back(base + 0); idx.push_back(base + 2); idx.push_back(base + 3);
    }
    if (bgfx::isValid(vbh_)) { bgfx::destroy(vbh_); vbh_ = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(ibh_)) { bgfx::destroy(ibh_); ibh_ = BGFX_INVALID_HANDLE; }
    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();
    vbh_ = bgfx::createVertexBuffer(bgfx::copy(verts.data(), static_cast<u32>(verts.size() * sizeof(GVertex))), layout);
    ibh_ = bgfx::createIndexBuffer(bgfx::copy(idx.data(), static_cast<u32>(idx.size() * sizeof(u32))), BGFX_BUFFER_INDEX32);
    quadCount_ = static_cast<u32>(clumps_.size()) * 2;
    builtHeight_ = heightFrac;
}

void GrassRenderer::destroy() {
    if (bgfx::isValid(vbh_)) bgfx::destroy(vbh_);
    if (bgfx::isValid(ibh_)) bgfx::destroy(ibh_);
    if (bgfx::isValid(grassTex_)) bgfx::destroy(grassTex_);
    if (bgfx::isValid(sampler_)) bgfx::destroy(sampler_);
    if (bgfx::isValid(fade_)) bgfx::destroy(fade_);
    if (bgfx::isValid(bias_)) bgfx::destroy(bias_);
    if (bgfx::isValid(program_)) bgfx::destroy(program_);
    vbh_ = BGFX_INVALID_HANDLE; ibh_ = BGFX_INVALID_HANDLE; grassTex_ = BGFX_INVALID_HANDLE;
    sampler_ = BGFX_INVALID_HANDLE; fade_ = BGFX_INVALID_HANDLE; bias_ = BGFX_INVALID_HANDLE;
    program_ = BGFX_INVALID_HANDLE;
    clumps_.clear();
    quadCount_ = 0;
    builtHeight_ = -1.0f;
    ready_ = false;
}

void GrassRenderer::render(double /*time*/) {
    if (!ready_ || !g_grassEnabled) return;
    if (g_grassHeightFrac != builtHeight_) buildMesh(g_grassHeightFrac);
    if (quadCount_ == 0 || !bgfx::isValid(vbh_)) return;
    const float fade[4] = {1.0f, 0.0f, 0.0f, 0.0f};
    const float bias[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    // Alpha-cutout foliage: opaque where the blade is (fs_sprite3d discards a<0.5), writes depth so it
    // occludes / is occluded correctly. No blending.
    const u64 state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;
    bgfx::setVertexBuffer(0, vbh_);
    bgfx::setIndexBuffer(ibh_);
    bgfx::setTexture(0, sampler_, grassTex_);
    bgfx::setUniform(fade_, fade);
    bgfx::setUniform(bias_, bias);
    bgfx::setState(state);
    bgfx::submit(0, program_);
}

} // namespace uaro

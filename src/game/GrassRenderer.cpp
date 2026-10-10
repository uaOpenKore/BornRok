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
// Tuft height and (half-)width are independent fractions of the Normals level (x1 = 0.20, x1.5 = 0.30,
// x2 = 0.40). Height was reduced 1.5x from the earlier 2.0 base (S.: "высоту уменьшить в 1.5 раза");
// width kept where it was. Decoupled so height/width tune separately.
constexpr float kHeightBase = 2.0f / 1.5f;  // ~1.333 (2.0 base, -1.5x height)
constexpr float kWidthBase = 0.9f;          // unchanged half-width (= old 0.45 * 2.0)
constexpr u32 kMaxClumps = 500000;  // cap the scatter so a huge field can't flood the frame pool

// Deterministic per-cell jitter in [0,1) (no RNG -> identical every load).
inline float hash01(u32 a, u32 b, u32 c) {
    u32 h = a * 73856093u ^ b * 19349663u ^ c * 83492791u;
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return static_cast<float>(h & 0xffffffu) / static_cast<float>(0x1000000u);
}

// A detailed procedural grass-tuft sprite (S.: make the tufts richer): ~14 curved, tapering blades of
// varied height/lean/brightness fanning out from the base, on a transparent background, tips at the
// top. GRAYSCALE — the actual colour is the per-tuft tint (the tile's mean colour); the slight
// per-blade + base-to-tip luminance variation only gives the blades depth, so the overall fill tone
// still tracks the tile average. Alpha-cutout silhouette.
bgfx::TextureHandle makeGrassTexture() {
    constexpr int W = 48, H = 64, NB = 14;
    struct Blade { float bx, lean, hFrac, baseHW, lum; };
    Blade bl[NB];
    for (int i = 0; i < NB; ++i) {
        const float u = (static_cast<float>(i) + 0.5f) / NB;
        bl[i].bx = (0.10f + 0.80f * u + (hash01(i, 11u, 1u) - 0.5f) * 0.06f) * W;  // base x, spread + jitter
        bl[i].lean = (hash01(i, 13u, 2u) - 0.5f) * 2.0f * 7.0f;                     // -7..7 px sideways curve
        bl[i].hFrac = 0.58f + 0.40f * hash01(i, 17u, 3u);                           // blade height (frac of H)
        bl[i].baseHW = 1.3f + 1.4f * hash01(i, 19u, 4u);                            // base half-width (px)
        bl[i].lum = 0.74f + 0.26f * hash01(i, 23u, 5u);                             // per-blade brightness
    }
    std::vector<u8> px(static_cast<usize>(W) * H * 4, 0);
    for (int y = 0; y < H; ++y) {
        const float yf = static_cast<float>(H - 1 - y) / (H - 1);  // 0 at base -> 1 at the very top
        for (int x = 0; x < W; ++x) {
            float bestCov = 0.0f, bestLum = 0.0f;
            for (int i = 0; i < NB; ++i) {
                if (yf > bl[i].hFrac) continue;                       // above this blade's tip
                const float yb = yf / bl[i].hFrac;                    // 0 base -> 1 this blade's tip
                const float cx = bl[i].bx + bl[i].lean * (yb * yb);   // parabolic lean (curves near tip)
                const float hw = std::max(0.35f, bl[i].baseHW * (1.0f - yb));  // taper to a point
                const float d = std::fabs(static_cast<float>(x) + 0.5f - cx);
                const float cov = std::clamp(hw - d + 0.5f, 0.0f, 1.0f);         // 1px soft edge
                if (cov > bestCov) { bestCov = cov; bestLum = bl[i].lum * (0.62f + 0.38f * yb); }
            }
            u8* o = &px[(static_cast<usize>(y) * W + x) * 4];
            if (bestCov < 0.5f) { o[3] = 0; continue; }  // outside the silhouette (cutout at 0.5)
            const u8 lum = static_cast<u8>(std::clamp(bestLum * 255.0f, 0.0f, 255.0f));
            o[0] = lum; o[1] = lum; o[2] = lum; o[3] = 255;
        }
    }
    return bgfx::createTexture2D(static_cast<u16>(W), static_cast<u16>(H), false, 1,
                                 bgfx::TextureFormat::RGBA8,
                                 BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
                                 bgfx::copy(px.data(), static_cast<u32>(px.size())));
}

// Mean colour over ALL pixels of a decoded texture (S.: the grass tone must equal the tile's average
// of every pixel). Cheap — run once per ground texture at load.
void avgColor(const Image& img, float& ar, float& ag, float& ab) {
    ar = ag = ab = 0.0f;
    if (!img.valid()) return;
    u64 r = 0, g = 0, b = 0;
    const usize n = static_cast<usize>(img.width) * img.height;
    for (usize i = 0; i < n; ++i) {
        const u8* p = &img.rgba[i * 4];
        r += p[0]; g += p[1]; b += p[2];
    }
    if (n == 0) return;
    ar = static_cast<float>(r) / n; ag = static_cast<float>(g) / n; ab = static_cast<float>(b) / n;
}

// Is the texture "grassy" (green-dominant field)? Green must lead red (excludes red-dominant sand/
// dirt/stone) AND clearly exceed the mean of red+blue — i.e. little red/blue beside the green (S.:
// "уменьшить если много красного/синего"). This rejects greenish-GREY mossy rock, whose red and blue
// sit much closer to green than real grass does. Grass tiles (118,125,54)/(98,102,41)/(82,87,31) have
// a green-excess ~30-39; mossy rock ~17; sand/stone are red-dominant.
bool isGrassy(float ar, float ag, float ab) {
    const float greenExcess = ag - (ar + ab) * 0.5f;  // how much green stands out over the red/blue mean
    return ag > 45.0f && ag >= ar && greenExcess > 28.0f;
}
}  // namespace

bool GrassRenderer::load(Application& app, const MapData& map, bool hasWater) {
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

    // Which ground textures are grass (green-dominant)? Also keep each tile's MEAN colour as the tuft
    // tint (S.: "цвет кустика ... по среднему цвету тайла"). Log the verdict for GPU-less diagnosis.
    std::vector<bool> grassy(map.textures.size(), false);
    std::vector<u32> texTint(map.textures.size(), 0xffffffffu);
    u32 grassyTex = 0;
    for (usize ti = 0; ti < map.textures.size(); ++ti) {
        if (!map.textures[ti]) continue;
        float ar, ag, ab;
        avgColor(*map.textures[ti], ar, ag, ab);
        // Tint = the tile's EXACT mean colour (S.: match the average of all pixels, no brightening).
        const auto ch = [](float v) { return static_cast<u32>(std::clamp(v, 0.0f, 255.0f)); };
        texTint[ti] = ch(ar) | (ch(ag) << 8) | (ch(ab) << 16) | 0xff000000u;  // R | G<<8 | B<<16 | A<<24
        const bool gr = isGrassy(ar, ag, ab);
        if (gr) { grassy[ti] = true; ++grassyTex; }
        if (ti < 20)
            log::info("Grass: tex[{}] avg rgb=({},{},{}) grassy={}", ti,
                      static_cast<int>(ar), static_cast<int>(ag), static_cast<int>(ab), gr ? 1 : 0);
    }
    log::info("GrassRenderer: {}/{} ground textures flagged grassy", grassyTex, map.textures.size());
    if (grassyTex == 0) {  // no grass on this map -> nothing to do (indoor/desert/etc.)
        bgfx::destroy(program_); program_ = BGFX_INVALID_HANDLE; return false;
    }

    // Skip cells that are under water: use the SAME test as WaterRenderer (a corner above the
    // level-waveHeight threshold means the ground there is below the water surface), so grass never
    // grows where water is drawn (S.: "трава ... под водой").
    const float wthresh = map.rsw.water().level - map.rsw.water().waveHeight;

    // Tufts per grass cell, spread EVENLY via stratified sampling (S.: "12 ... равномерно расбрасывать"):
    // split the cell into a gx*gy grid and drop one jittered tuft per sub-cell, so they cover the tile
    // uniformly instead of clumping like pure random jitter did.
    constexpr int kPerCell = 12;
    constexpr int kGx = 4, kGy = 3;  // 4*3 = 12 sub-cells
    for (u32 y = 0; y < H && clumps_.size() < kMaxClumps; ++y) {
        for (u32 x = 0; x < W && clumps_.size() < kMaxClumps; ++x) {
            const GndCube& c = cubes[x + y * W];
            if (c.tileUp < 0 || static_cast<usize>(c.tileUp) >= surfs.size()) continue;
            const int tid = surfs[c.tileUp].textureId;
            if (tid < 0 || static_cast<usize>(tid) >= grassy.size() || !grassy[tid]) continue;
            // Under-water cell (any corner below the water surface) -> no grass. Only when the map
            // actually has water (else the water level is meaningless and would wrongly cull the field).
            if (hasWater && (c.height[0] > wthresh || c.height[1] > wthresh ||
                             c.height[2] > wthresh || c.height[3] > wthresh)) continue;
            // Steep cell (cliff / rock face) -> no grass: grass fields are near-flat, rock slopes aren't
            // (S.: трава лезет на камни). Skip when the corner height spread is large (> ~1.2 world units).
            float hmin = c.height[0], hmax = c.height[0];
            for (int k = 1; k < 4; ++k) { hmin = std::min(hmin, c.height[k]); hmax = std::max(hmax, c.height[k]); }
            if (hmax - hmin > 12.0f) continue;
            const u32 tint = texTint[tid];
            for (int k = 0; k < kPerCell; ++k) {
                // Stratified: sub-cell (sc,sr) + a jittered position inside it -> even coverage.
                const int sc = k % kGx, sr = k / kGx;
                const float jx = (static_cast<float>(sc) + 0.2f + 0.6f * hash01(x, y, 1u + k * 7u)) / kGx;
                const float jz = (static_cast<float>(sr) + 0.2f + 0.6f * hash01(x, y, 2u + k * 7u)) / kGy;
                // Bilinear corner height (same mapping as MapRenderer::heightAt), world Y = -h*0.1.
                const float top = c.height[0] + (c.height[1] - c.height[0]) * jx;
                const float bot = c.height[2] + (c.height[3] - c.height[2]) * jx;
                const float gy = -(top + (bot - top) * jz) * 0.1f;
                const float wx = static_cast<float>(W) - (static_cast<float>(x) + jx);  // X mirror (world = W - cell)
                const float wz = static_cast<float>(y) + jz;
                clumps_.push_back({wx, wz, gy, tint});
            }
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
    const float frac = std::clamp(heightFrac, 0.05f, 1.0f);
    const float h = kHeightBase * frac;    // tuft height
    const float hw = kWidthBase * frac;    // tuft half-width (independent of height now)
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

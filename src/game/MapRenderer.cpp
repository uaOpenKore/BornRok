#include "game/MapRenderer.hpp"

#include <algorithm>
#include <cmath>

#include "app/Application.hpp"
#include "core/Log.hpp"
#include "formats/ImageIO.hpp"
#include "render/SamplerFilter.hpp"
#include "render/Shader.hpp"
#include "world/MapLoader.hpp"

namespace uaro {

namespace {
// Build a tightly-packed RGBA mip chain (2x2 box filter) so ground textures can
// use mipmaps + anisotropic filtering — removes the shimmer/moire at distance.
std::vector<u8> buildMipChain(const Image& img) {
    std::vector<u8> out(img.rgba);  // level 0
    std::vector<u8> prev = img.rgba;
    u32 pw = img.width, ph = img.height;
    while (pw > 1 || ph > 1) {
        const u32 nw = std::max<u32>(1, pw >> 1);
        const u32 nh = std::max<u32>(1, ph >> 1);
        std::vector<u8> cur(static_cast<usize>(nw) * nh * 4);
        for (u32 y = 0; y < nh; ++y) {
            const u32 y0 = std::min(ph - 1, y * 2), y1 = std::min(ph - 1, y * 2 + 1);
            for (u32 x = 0; x < nw; ++x) {
                const u32 x0 = std::min(pw - 1, x * 2), x1 = std::min(pw - 1, x * 2 + 1);
                for (int c = 0; c < 4; ++c) {
                    const u32 s = prev[(y0 * pw + x0) * 4 + c] + prev[(y0 * pw + x1) * 4 + c] +
                                  prev[(y1 * pw + x0) * 4 + c] + prev[(y1 * pw + x1) * 4 + c];
                    cur[(y * nw + x) * 4 + c] = static_cast<u8>(s / 4);
                }
            }
        }
        out.insert(out.end(), cur.begin(), cur.end());
        prev.swap(cur);
        pw = nw;
        ph = nh;
    }
    return out;
}
} // namespace

bool MapRenderer::load(Application& app, const std::string& mapName) {
    auto md = MapLoader::load(app.vfs(), mapName);
    if (!md) {
        log::error("MapRenderer: failed to load map '{}'", mapName);
        return false;
    }
    map_ = std::move(*md);
    if (map_.ground.vertices.empty()) {
        log::warn("MapRenderer: '{}' has no ground geometry", mapName);
        return false;
    }

    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();
    vbh_ = bgfx::createVertexBuffer(
        bgfx::copy(map_.ground.vertices.data(),
                   static_cast<u32>(map_.ground.vertices.size() * sizeof(GroundVertex))),
        layout);
    ibh_ = bgfx::createIndexBuffer(
        bgfx::copy(map_.ground.indices.data(),
                   static_cast<u32>(map_.ground.indices.size() * sizeof(u32))),
        BGFX_BUFFER_INDEX32);

    sampler_ = bgfx::createUniform("s_tex", bgfx::UniformType::Sampler);
    const u8 whitePx[4] = {255, 255, 255, 255};
    white_ = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE,
                                   bgfx::copy(whitePx, 4));

    // CLAMP wrap kills the ground texture seams (S.: "полоски сгонки текстур ... свести 1 пиксель"):
    // ~87% of GND tiles sit on the texture border (u/v at 0 or 1), and with the default REPEAT wrap
    // the bilinear/anisotropic tap there bleeds in the OPPOSITE edge's texels — a thin line at every
    // tile join, worst at the lower mips. No tile tiles past [0,1] (verified across dungeon+city maps
    // via maptest UVRANGE), so clamping to the edge is lossless for the tiling and removes the bleed.
    constexpr u64 kTexFlags = BGFX_SAMPLER_MIN_ANISOTROPIC | BGFX_SAMPLER_MAG_ANISOTROPIC |
                              BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
    textures_.reserve(map_.textures.size());
    for (const auto& img : map_.textures) {
        if (img && img->valid()) {
            // Punch out the magenta colour-key (#ff00ff) to alpha 0 so fs_ground can discard it —
            // RO's floating walkways/platforms use it for their transparent border (S.: "розовый
            // должен быть прозрачным") — and despill the anti-aliased rim so a PNG override with
            // soft edges over magenta doesn't leave a pink halo (shared with the model path).
            Image keyed = *img;
            keyAndDespillMagenta(keyed);
            const std::vector<u8> chain = buildMipChain(keyed);
            textures_.push_back(bgfx::createTexture2D(
                static_cast<u16>(keyed.width), static_cast<u16>(keyed.height), true, 1,
                bgfx::TextureFormat::RGBA8, kTexFlags,
                bgfx::copy(chain.data(), static_cast<u32>(chain.size()))));
        } else {
            textures_.push_back(white_);
        }
    }

    // Optional normal maps (#107): for each ground tile texture, try <name>_n.png. Missing
    // maps fall back to flatNrm_ (tangent-space up) so the shader's slot-2 sampler is always
    // bound and a map without any _n.png renders byte-identically to before. Mip + aniso like
    // the diffuse tiles so the relief doesn't shimmer at distance.
    nrmSampler_ = bgfx::createUniform("s_nrm", bgfx::UniformType::Sampler);
    nrmParams_ = bgfx::createUniform("u_nrmParams", bgfx::UniformType::Vec4);
    lightDir_ = bgfx::createUniform("u_lightDir", bgfx::UniformType::Vec4);
    const u8 flatPx[4] = {128, 128, 255, 255};  // (0,0,1) in tangent space = no perturbation
    flatNrm_ = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE,
                                     bgfx::copy(flatPx, 4));
    normalTextures_.reserve(map_.gnd.textures().size());
    for (usize ti = 0; ti < map_.gnd.textures().size(); ++ti) {
        const auto& name = map_.gnd.textures()[ti];
        bgfx::TextureHandle h = flatNrm_;
        // Optional hand-authored normal map: probe _n.webp then _n.png QUIETLY — most tiles have none, and
        // all content ships as .webp, so an absent _n.png must NOT spam the missing-resource log (S.: "в лог
        // не должно писаться отсутствующая текстура, если её находит по webp — у нас все изображения в webp").
        std::optional<std::vector<u8>> nbytes;
        {
            const std::string np = normalMapPath("data/texture/" + name);  // ..._n.png
            if (np.size() > 4) nbytes = app.vfs().readQuiet(np.substr(0, np.size() - 4) + ".webp");
            if (!nbytes) nbytes = app.vfs().readQuiet(np);
        }
        if (nbytes) {
            if (auto img = decodeImage(*nbytes); img && img->valid()) {
                const std::vector<u8> chain = buildMipChain(*img);
                h = bgfx::createTexture2D(
                    static_cast<u16>(img->width), static_cast<u16>(img->height), true, 1,
                    bgfx::TextureFormat::RGBA8, kTexFlags,
                    bgfx::copy(chain.data(), static_cast<u32>(chain.size())));
            }
        } else if (ti < map_.textures.size() && map_.textures[ti] && map_.textures[ti]->valid()) {
            // No hand-authored _n.png: derive relief from the diffuse's own luminance (S.:
            // "исходя из яркости пикселя сделать высоту нормали"). Subtle strength — the dark
            // seams of stone/brick become grooves without over-embossing painted detail.
            const Image gen = normalFromLuminance(*map_.textures[ti], 3.0f);  // x2 base; scaled live by g_normalsFactor
            if (gen.valid()) {
                const std::vector<u8> chain = buildMipChain(gen);
                h = bgfx::createTexture2D(
                    static_cast<u16>(gen.width), static_cast<u16>(gen.height), true, 1,
                    bgfx::TextureFormat::RGBA8, kTexFlags,
                    bgfx::copy(chain.data(), static_cast<u32>(chain.size())));
            }
        }
        normalTextures_.push_back(h);
    }

    // Lightmap texture (baked shadows + coloured light), sampled per-pixel by the
    // ground shader for smooth shadows. Bilinear + clamp so it interpolates across
    // cells; a map without lightmaps uploads a 1x1 neutral texel (full bright, no
    // colour) so the shader reduces to plain ambient+diffuse lighting.
    lightmapSampler_ = bgfx::createUniform("s_lightmap", bgfx::UniformType::Sampler);
    mapDim_ = bgfx::createUniform("u_mapDim", bgfx::UniformType::Vec4);
    ambient_ = bgfx::createUniform("u_ambient", bgfx::UniformType::Vec4);
    diffuse_ = bgfx::createUniform("u_diffuse", bgfx::UniformType::Vec4);
    fade_ = bgfx::createUniform("u_fade", bgfx::UniformType::Vec4);  // ground x-ray opacity (#104)
    {
        int lw = 0, lh = 0;
        const std::vector<u8> limg = GroundMesh::buildLightmap(map_.gnd, lw, lh);
        // Trilinear + mipmaps on the lightmap (S.: "трилинейные мипы на лайтмапу"): no POINT flag =
        // linear min/mag, and with a mip chain bgfx does linear-between-mips (trilinear), so distant
        // shadows resolve to the smoother lower mips instead of shimmering/jagged. Matches GRF Editor's
        // "Enable mipmapping".
        const u64 lmFlags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
        if (!limg.empty() && lw > 0 && lh > 0) {
            Image lmImg;
            lmImg.width = static_cast<u32>(lw);
            lmImg.height = static_cast<u32>(lh);
            lmImg.rgba = limg;
            const std::vector<u8> chain = buildMipChain(lmImg);  // 2x2 box mips, tightly packed
            lightmap_ = bgfx::createTexture2D(
                static_cast<u16>(lw), static_cast<u16>(lh), true /*mips*/, 1,
                bgfx::TextureFormat::RGBA8, lmFlags,
                bgfx::copy(chain.data(), static_cast<u32>(chain.size())));
        } else {
            const u8 neutral[4] = {0, 0, 0, 255};  // no coloured light, full brightness
            lightmap_ = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, lmFlags,
                                              bgfx::copy(neutral, 4));
        }
    }

    // Per-cell environment light for tinting actors (#118): RSW ambient + diffuse * baked shadow, so a
    // sprite standing in a building's shadow is dimmed (RO does this). Same world->cell mapping as
    // heightAt; shadow = the cell's average lightmap alpha, gamma-matched (1.9) to the ground shader.
    {
        const Gnd& g = map_.gnd;
        const int W = static_cast<int>(g.width()), H = static_cast<int>(g.height());
        const int lmW = g.lightmapW(), lmH = g.lightmapH();
        const std::vector<u8>& lm = g.lightmap();
        const auto& cubes = g.cubes();
        const auto& surfs = g.surfaces();
        const RswLight& L = map_.rsw.light();
        cellLight_.assign(static_cast<usize>(std::max(0, W)) * std::max(0, H), Vec3{1.0f, 1.0f, 1.0f});
        const usize tileStride = static_cast<usize>(lmW) * lmH * 4;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                const GndCube& c = cubes[static_cast<usize>(y) * W + x];
                float s = 1.0f;
                if (c.tileUp >= 0 && static_cast<usize>(c.tileUp) < surfs.size()) {
                    const int lid = surfs[c.tileUp].lightmapId;
                    if (lid >= 0 && static_cast<usize>(lid) * tileStride + tileStride <= lm.size()) {
                        const usize tb = static_cast<usize>(lid) * tileStride;
                        int sum = 0;
                        for (int k = 0; k < lmW * lmH; ++k) sum += lm[tb + static_cast<usize>(k)];
                        if (lmW * lmH > 0) s = static_cast<float>(sum) / (lmW * lmH) / 255.0f;
                    }
                }
                s = std::pow(s, 1.9f);  // match the ground shader's kShadowGamma
                cellLight_[static_cast<usize>(y) * W + x] = {
                    std::clamp(L.ambient[0] + L.diffuse[0] * s, 0.0f, 1.0f),
                    std::clamp(L.ambient[1] + L.diffuse[1] * s, 0.0f, 1.0f),
                    std::clamp(L.ambient[2] + L.diffuse[2] * s, 0.0f, 1.0f)};
            }
    }

    program_ = load_program(app.assetDir(), "vs_ground", "fs_ground");
    if (!bgfx::isValid(program_))
        log::warn("MapRenderer: ground shader program unavailable (shaders not compiled?)");

    float minx = 1e30f, maxx = -1e30f, miny = 1e30f, maxy = -1e30f, minz = 1e30f, maxz = -1e30f;
    for (const auto& v : map_.ground.vertices) {
        minx = std::min(minx, v.x); maxx = std::max(maxx, v.x);
        miny = std::min(miny, v.y); maxy = std::max(maxy, v.y);
        minz = std::min(minz, v.z); maxz = std::max(maxz, v.z);
    }
    center_ = {(minx + maxx) * 0.5f, (miny + maxy) * 0.5f, (minz + maxz) * 0.5f};
    radius_ = std::max(maxx - minx, maxz - minz) * 0.6f;
    if (radius_ < 1.0f) radius_ = 100.0f;

    ready_ = true;
    log::info("MapRenderer: '{}' ready ({} verts, {} textures, {} batches)", mapName,
              map_.ground.vertices.size(), map_.textures.size(), map_.ground.batches.size());

    models_.load(app, map_);  // RSM objects (best-effort; ground renders regardless)
    water_.load(app, map_);   // animated water surface (best-effort; skipped if none)
    return true;
}

void MapRenderer::destroy() {
    for (auto t : textures_)
        if (bgfx::isValid(t) && t.idx != white_.idx) bgfx::destroy(t);
    textures_.clear();
    for (auto t : normalTextures_)
        if (bgfx::isValid(t) && t.idx != flatNrm_.idx) bgfx::destroy(t);
    normalTextures_.clear();
    if (bgfx::isValid(flatNrm_)) bgfx::destroy(flatNrm_);
    if (bgfx::isValid(nrmSampler_)) bgfx::destroy(nrmSampler_);
    if (bgfx::isValid(nrmParams_)) bgfx::destroy(nrmParams_);
    if (bgfx::isValid(lightDir_)) bgfx::destroy(lightDir_);
    if (bgfx::isValid(white_)) bgfx::destroy(white_);
    if (bgfx::isValid(vbh_)) bgfx::destroy(vbh_);
    if (bgfx::isValid(ibh_)) bgfx::destroy(ibh_);
    if (bgfx::isValid(sampler_)) bgfx::destroy(sampler_);
    if (bgfx::isValid(program_)) bgfx::destroy(program_);
    if (bgfx::isValid(lightmap_)) bgfx::destroy(lightmap_);
    if (bgfx::isValid(lightmapSampler_)) bgfx::destroy(lightmapSampler_);
    if (bgfx::isValid(mapDim_)) bgfx::destroy(mapDim_);
    if (bgfx::isValid(ambient_)) bgfx::destroy(ambient_);
    if (bgfx::isValid(diffuse_)) bgfx::destroy(diffuse_);
    if (bgfx::isValid(fade_)) bgfx::destroy(fade_);
    models_.destroy();
    water_.destroy();
    white_ = BGFX_INVALID_HANDLE;
    flatNrm_ = BGFX_INVALID_HANDLE;
    nrmSampler_ = BGFX_INVALID_HANDLE;
    nrmParams_ = BGFX_INVALID_HANDLE;
    lightDir_ = BGFX_INVALID_HANDLE;
    vbh_ = BGFX_INVALID_HANDLE;
    ibh_ = BGFX_INVALID_HANDLE;
    sampler_ = BGFX_INVALID_HANDLE;
    program_ = BGFX_INVALID_HANDLE;
    lightmap_ = BGFX_INVALID_HANDLE;
    lightmapSampler_ = BGFX_INVALID_HANDLE;
    mapDim_ = BGFX_INVALID_HANDLE;
    ambient_ = BGFX_INVALID_HANDLE;
    diffuse_ = BGFX_INVALID_HANDLE;
    fade_ = BGFX_INVALID_HANDLE;
    ready_ = false;
}

float MapRenderer::heightAt(float wx, float wz) const {
    // O(1) grid lookup (the old brute-force nearest-vertex scan was O(verts) per
    // call, called several times a frame). The ground is baked with X mirrored
    // (x -> W - x), so un-mirror to index the GND grid; Z maps straight through.
    // Bilinear over the cube's 4 corner heights, then to world Y like makeVert
    // (-h * 0.1) so terrain and models share one Y space.
    const Gnd& g = map_.gnd;
    const int W = static_cast<int>(g.width()), H = static_cast<int>(g.height());
    if (W <= 0 || H <= 0 || g.cubes().empty()) return center_.y;
    const float gx = static_cast<float>(W) - wx, gz = wz;
    const int ix = std::clamp(static_cast<int>(std::floor(gx)), 0, W - 1);
    const int iy = std::clamp(static_cast<int>(std::floor(gz)), 0, H - 1);
    const float fx = std::clamp(gx - static_cast<float>(ix), 0.0f, 1.0f);
    const float fy = std::clamp(gz - static_cast<float>(iy), 0.0f, 1.0f);
    const GndCube& c = g.cubes()[static_cast<usize>(iy) * W + ix];
    const float top = c.height[0] + (c.height[1] - c.height[0]) * fx;
    const float bot = c.height[2] + (c.height[3] - c.height[2]) * fx;
    return -(top + (bot - top) * fy) * 0.1f;
}

Vec3 MapRenderer::lightAt(float wx, float wz) const {
    const Gnd& g = map_.gnd;
    const int W = static_cast<int>(g.width()), H = static_cast<int>(g.height());
    if (W <= 0 || H <= 0 || cellLight_.empty()) return Vec3{1.0f, 1.0f, 1.0f};
    const int ix = std::clamp(static_cast<int>(std::floor(static_cast<float>(W) - wx)), 0, W - 1);
    const int iy = std::clamp(static_cast<int>(std::floor(wz)), 0, H - 1);  // same map as heightAt
    return cellLight_[static_cast<usize>(iy) * W + ix];
}

void MapRenderer::render(const Mat4& view, const Mat4& proj, double time, const Vec3& camPos,
                         const Vec3& playerPos, float worldFade) const {
    if (!ready_ || !bgfx::isValid(program_)) return;
    bgfx::setViewTransform(0, view.m, proj.m);

    // Lighting uniforms: map size (for the lightmap UV) + the RSW global ambient/diffuse.
    const RswLight& L = map_.rsw.light();
    const float mapDim[4] = {static_cast<float>(map_.gnd.width()),
                             static_cast<float>(map_.gnd.height()), 0.0f, 0.0f};
    // Clamp very high RSW ambient: indoor maps (prt_in = 0.8,0.8,1.0) authored their ambient far
    // higher than outdoor (~0.3) and blew out to white with our flat application (S.: "prt_in очень
    // яркая, остальные локи нормально"). 0.55 = prontera's ambient, which reads correctly. Outdoor
    // maps (<=0.55) are untouched.
    const float kAmbMax = 0.55f;
    const float amb[4] = {std::min(L.ambient[0], kAmbMax), std::min(L.ambient[1], kAmbMax),
                          std::min(L.ambient[2], kAmbMax), 1.0f};
    const float dif[4] = {L.diffuse[0], L.diffuse[1], L.diffuse[2], 1.0f};
    // RSW sun direction (lat/long -> unit vector), same derivation as ModelRenderer, for the
    // optional normal-map N·L relief. Only consumed when a tile ships a _n.png. (#107)
    const float lon = radians(static_cast<float>(L.longitude));
    const float lat = radians(static_cast<float>(L.latitude));
    const float ldir[4] = {std::cos(lat) * std::sin(lon), std::cos(lon),
                           std::sin(lat) * std::sin(lon), 0.0f};

    // x-ray (worldFade<1): blend the ground at the given opacity but KEEP writing depth, so the
    // feet-occlusion test driving the x-ray stays stable (a non-Z fade flips visible<->occluded every
    // frame -> flicker). Opaque otherwise. (#104)
    const bool xray = worldFade < 0.999f;
    const u64 state = xray ? (BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS |
                              BGFX_STATE_BLEND_ALPHA)
                           : (BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z |
                              BGFX_STATE_DEPTH_TEST_LESS);  // no culling for now
    const float fadeVec[4] = {xray ? worldFade : 1.0f, 0.0f, 0.0f, 0.0f};
    for (const auto& b : map_.ground.batches) {
        bgfx::TextureHandle tex = white_;
        if (b.textureId >= 0 && static_cast<usize>(b.textureId) < textures_.size() &&
            bgfx::isValid(textures_[b.textureId]))
            tex = textures_[b.textureId];

        bgfx::setVertexBuffer(0, vbh_);
        bgfx::setIndexBuffer(ibh_, b.indexStart, b.indexCount);
        // World texture filter from Setup Video (#104): 0=point (crisp), else linear. Keep the clamp.
        const u32 wFlags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
                           (g_worldFilterMode == 0 ? BGFX_SAMPLER_POINT : 0u);
        // Slot-2 normal map (#107): the tile's <name>_n.png if present, else flatNrm_ (no relief).
        // s_nrm must always be bound (DX11 requires every declared sampler set) — hasNrm gates the
        // shader's lighting branch so the flat fallback is a no-op.
        bgfx::TextureHandle nrm = flatNrm_;
        bool hasNrm = false;
        if (b.textureId >= 0 && static_cast<usize>(b.textureId) < normalTextures_.size() &&
            bgfx::isValid(normalTextures_[b.textureId]) &&
            normalTextures_[b.textureId].idx != flatNrm_.idx) {
            nrm = normalTextures_[b.textureId];
            hasNrm = true;
        }
        const float nrmParams[4] = {(hasNrm && g_normalsFactor > 0.001f) ? 1.0f : 0.0f,
                                    g_normalsFactor, 0.0f, 0.0f};  // y = live bump strength (Normals toggle)

        bgfx::setTexture(0, sampler_, tex, wFlags);
        bgfx::setTexture(1, lightmapSampler_, lightmap_);
        bgfx::setTexture(2, nrmSampler_, nrm, wFlags);
        bgfx::setUniform(mapDim_, mapDim);
        bgfx::setUniform(ambient_, amb);
        bgfx::setUniform(diffuse_, dif);
        bgfx::setUniform(fade_, fadeVec);
        bgfx::setUniform(nrmParams_, nrmParams);
        bgfx::setUniform(lightDir_, ldir);
        bgfx::setState(state);
        bgfx::submit(0, program_);
    }

    models_.render(map_, camPos, playerPos, time, worldFade);  // RSM objects (faded too in x-ray; time spins animated nodes)
    water_.render(time);   // animated water surface, blended over the terrain it covers
}

} // namespace uaro

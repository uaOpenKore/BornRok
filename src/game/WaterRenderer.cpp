#include "game/WaterRenderer.hpp"

#include <algorithm>
#include <cstdio>
#include <string>

#include "app/Application.hpp"
#include "core/Log.hpp"
#include "render/Shader.hpp"
#include "world/MapData.hpp"

#include "formats/ImageIO.hpp"

namespace uaro {

namespace {
struct WVertex {
    f32 x, y, z, u, v;
    u32 abgr;
};
constexpr float kWaterAlpha = 0.55f;  // surface translucency
// 워터 (water texture folder), EUC-KR.
const char* const kWaterDir = "data/texture/\xbf\xf6\xc5\xcd/";

bgfx::TextureHandle decodeJpgToTexture(const std::vector<u8>& bytes) {
    auto img = decodeImage(bytes);  // JPG/PNG/BMP/TGA by magic (water textures are usually JPG)
    if (!img || !img->valid()) return BGFX_INVALID_HANDLE;
    // Default (wrap) sampling so the tiled UVs repeat the texture across the map.
    return bgfx::createTexture2D(
        static_cast<u16>(img->width), static_cast<u16>(img->height), false, 1,
        bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_NONE,
        bgfx::copy(img->rgba.data(), static_cast<u32>(img->rgba.size())));
}
}  // namespace

bool WaterRenderer::load(Application& app, const MapData& map) {
    // Pair fs_sprite3d with vs_sprite3d, NOT vs_model: vs_model emits v_wnormal/v_wtangent varyings
    // that fs_sprite3d doesn't read, so that program fails to LINK on strict backends (DX11) and the
    // water silently vanished (S. log: "createProgram vs_model+fs_sprite3d FAILED (dx11)"). vs_sprite3d
    // does the same u_modelViewProj transform and outputs exactly {v_texcoord0,v_color0}; its extra
    // u_spriteBias depth nudge is zeroed at draw time so the flat plane isn't pushed off its depth.
    program_ = load_program(app.assetDir(), "vs_sprite3d", "fs_sprite3d");
    if (!bgfx::isValid(program_)) {
        log::warn("WaterRenderer: shader unavailable; no water");
        return false;
    }

    // Water plane Y uses the same -h*0.1 world convention as the ground/models, so it
    // sits in one Y space with the terrain. Span the whole map in X/Z.
    const float waterY = -map.rsw.water().level * 0.1f;
    const u32 W = map.gnd.width(), H = map.gnd.height();
    if (W == 0 || H == 0 || map.ground.vertices.empty()) {
        bgfx::destroy(program_);
        program_ = BGFX_INVALID_HANDLE;
        return false;
    }
    // Build the water surface PER CELL, exactly like roBrowser (Ground.js compile): a cell gets a
    // water quad only when it has a top face AND at least one corner dips below the water plane
    // (height > level - waveHeight in RO's raw height units -> the ground there sits under the
    // surface). This replaces the old single map-wide quad + terrain min/max cull, which could only
    // flood or hide the whole map and so missed canal maps where water fills just the sunken
    // channels (ptr_sewb1 — S.: "в каналах должна быть вода"). Indoor maps whose leftover water.level
    // is under every floor produce no qualifying cells, so they stay dry with no special-case.
    const auto& cubes = map.gnd.cubes();
    if (cubes.size() < static_cast<usize>(W) * H) {
        bgfx::destroy(program_);
        program_ = BGFX_INVALID_HANDLE;
        return false;
    }
    const float level = map.rsw.water().level;
    const float threshold = level - map.rsw.water().waveHeight;  // corner above this => underwater
    auto urepeat = [](u32 a) { return static_cast<float>(a % 5) / 5.0f; };  // texture tiles every 5 cells
    std::vector<WVertex> verts;
    std::vector<u32> idx;
    // Diagnostics (S.: canal water not appearing) -- the GND height range vs the water level tells us
    // at a glance whether ANY cell is underwater and whether the height sign is what we expect.
    float minH = 1e9f, maxH = -1e9f;
    u32 topCells = 0, underCells = 0;
    for (u32 y = 0; y < H; ++y) {
        for (u32 x = 0; x < W; ++x) {
            const GndCube& c = cubes[x + y * W];
            for (int k = 0; k < 4; ++k) { minH = std::min(minH, c.height[k]); maxH = std::max(maxH, c.height[k]); }
            const bool under = c.height[0] > threshold || c.height[1] > threshold ||
                               c.height[2] > threshold || c.height[3] > threshold;
            if (under) ++underCells;
            if (c.tileUp < 0) continue;  // no top surface -> no water (roBrowser gates on tile_up)
            ++topCells;
            if (!under) continue;  // this cell's ground stays above the water plane
            // Mirror X exactly like GroundMesh (xm = W - x) -- it bakes an X flip into the terrain for
            // the original client's handedness. The old map-wide quad spanned 0..W symmetrically so the
            // flip didn't matter, but a PER-CELL quad must sit on the mirrored cell or it lands on the
            // dry walkway (mirror image of the channel) and is hidden under the terrain (S.: no canal water).
            const float x0 = static_cast<float>(W - x), x1 = static_cast<float>(W - (x + 1));
            const float z0 = static_cast<float>(y), z1 = z0 + 1.0f;
            const float u0 = urepeat(x);
            float u1 = urepeat(x + 1);
            if (u1 == 0.0f) u1 = 1.0f;  // ||1 wrap so a quad ending on a 5-cell boundary spans a full tile
            const float v0 = urepeat(y);
            float v1 = urepeat(y + 1);
            if (v1 == 0.0f) v1 = 1.0f;
            const u32 b = static_cast<u32>(verts.size());
            verts.push_back({x0, waterY, z0, u0, v0, 0xffffffffu});
            verts.push_back({x1, waterY, z0, u1, v0, 0xffffffffu});
            verts.push_back({x1, waterY, z1, u1, v1, 0xffffffffu});
            verts.push_back({x0, waterY, z1, u0, v1, 0xffffffffu});
            idx.push_back(b + 0); idx.push_back(b + 1); idx.push_back(b + 2);
            idx.push_back(b + 0); idx.push_back(b + 2); idx.push_back(b + 3);
        }
    }
    if (verts.empty()) {
        // underCells>0 but no quads => the underwater cells lack a top tile (channel bottom is an RSM
        // model, not GND). underCells==0 with maxH<=level => nothing dips below; if maxH is well above
        // level the sign/scale would be wrong (it isn't -- see below), so this is genuinely a dry GND.
        log::info("WaterRenderer: no water. level={} threshold={} GND height range [{}..{}] "
                  "underwaterCells={} cellsWithTopTile={}",
                  level, threshold, minH, maxH, underCells, topCells);
        bgfx::destroy(program_);
        program_ = BGFX_INVALID_HANDLE;
        return false;
    }

    animSpeed_ = map.rsw.water().animSpeed > 0 ? map.rsw.water().animSpeed : 3;
    const int type = std::clamp(map.rsw.water().type, 0, 7);

    // Decode the 32 animation frames water<type>00..31.jpg.
    for (int nn = 0; nn < 32; ++nn) {
        char name[32];
        std::snprintf(name, sizeof(name), "water%d%02d.jpg", type, nn);
        bgfx::TextureHandle t = BGFX_INVALID_HANDLE;
        if (auto bytes = app.vfs().readPreferPng(std::string(kWaterDir) + name)) t = decodeJpgToTexture(*bytes);  // hi-res .png over .jpg (S.)
        if (bgfx::isValid(t)) frames_.push_back(t);
    }
    if (frames_.empty()) {
        log::warn("WaterRenderer: no water textures decoded (type {})", type);
        destroy();
        return false;
    }

    sampler_ = bgfx::createUniform("s_tex", bgfx::UniformType::Sampler);
    fade_ = bgfx::createUniform("u_spriteFade", bgfx::UniformType::Vec4);
    bias_ = bgfx::createUniform("u_spriteBias", bgfx::UniformType::Vec4);  // vs_sprite3d depth nudge (kept 0)

    // Upload the per-cell water mesh built above (only the underwater cells).
    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();
    vbh_ = bgfx::createVertexBuffer(
        bgfx::copy(verts.data(), static_cast<u32>(verts.size() * sizeof(WVertex))), layout);
    ibh_ = bgfx::createIndexBuffer(
        bgfx::copy(idx.data(), static_cast<u32>(idx.size() * sizeof(u32))), BGFX_BUFFER_INDEX32);

    ready_ = true;
    log::info("WaterRenderer: type {} ({} frames) at y={}, {} water cells on {}x{} "
              "(level={} GND height [{}..{}])",
              type, frames_.size(), waterY, verts.size() / 4, W, H, level, minH, maxH);
    return true;
}

void WaterRenderer::destroy() {
    for (auto t : frames_)
        if (bgfx::isValid(t)) bgfx::destroy(t);
    frames_.clear();
    if (bgfx::isValid(vbh_)) bgfx::destroy(vbh_);
    if (bgfx::isValid(ibh_)) bgfx::destroy(ibh_);
    if (bgfx::isValid(sampler_)) bgfx::destroy(sampler_);
    if (bgfx::isValid(fade_)) bgfx::destroy(fade_);
    if (bgfx::isValid(bias_)) bgfx::destroy(bias_);
    if (bgfx::isValid(program_)) bgfx::destroy(program_);
    vbh_ = BGFX_INVALID_HANDLE;
    ibh_ = BGFX_INVALID_HANDLE;
    sampler_ = BGFX_INVALID_HANDLE;
    fade_ = BGFX_INVALID_HANDLE;
    bias_ = BGFX_INVALID_HANDLE;
    program_ = BGFX_INVALID_HANDLE;
    ready_ = false;
}

void WaterRenderer::render(double time) const {
    if (!ready_ || frames_.empty()) return;
    // Cycle one texture every ~animSpeed*50ms (animSpeed 3 -> 0.15s/frame, ~5s loop).
    const double framePeriod = 0.05 * static_cast<double>(animSpeed_ > 0 ? animSpeed_ : 3);
    int frame = static_cast<int>(time / framePeriod) % static_cast<int>(frames_.size());
    if (frame < 0) frame = 0;

    const float fade[4] = {kWaterAlpha, 0.0f, 0.0f, 0.0f};
    // Translucent, depth-TESTED against the terrain (LEQUAL) so it only shows where the
    // ground dips below the water level, but it does NOT write depth — so actors/objects
    // drawn afterwards stay visible rather than being clipped by the water plane. Drawn
    // after the opaque ground/models.
    const u64 state = BGFX_STATE_WRITE_RGB | BGFX_STATE_DEPTH_TEST_LEQUAL | BGFX_STATE_BLEND_ALPHA;
    const float bias[4] = {0.0f, 0.0f, 0.0f, 0.0f};  // flat plane -> no per-layer depth bias
    bgfx::setVertexBuffer(0, vbh_);
    bgfx::setIndexBuffer(ibh_);
    bgfx::setTexture(0, sampler_, frames_[frame]);
    bgfx::setUniform(fade_, fade);
    bgfx::setUniform(bias_, bias);
    bgfx::setState(state);
    bgfx::submit(0, program_);
}

} // namespace uaro

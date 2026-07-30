#pragma once
#include <bgfx/bgfx.h>

#include <string>
#include <vector>

#include "core/math/Math.hpp"
#include "game/ModelRenderer.hpp"
#include "game/WaterRenderer.hpp"
#include "world/MapData.hpp"

namespace uaro {

class Application;

// Owns the GPU resources for one map's ground mesh and draws it with a
// caller-supplied camera. Shared by the offline MapScene (orbit camera) and the
// in-game GameScene (player-centred camera). Ground only for now; RSM models,
// water and effects are layered on later.
class MapRenderer {
public:
    bool load(Application& app, const std::string& mapName);
    void destroy();
    bool ready() const { return ready_; }

    // Sets the view 0 transform and submits every ground batch, the models, and the
    // animated water surface (which uses `time` to pick its frame). camPos/playerPos
    // (world space) drive the model camera-occlude fade; leave them equal (the
    // default) to disable the fade, e.g. for the dev orbit viewer with no player.
    // worldFade < 1 draws the ground + all models semi-transparent (Camera Lock x-ray: see the floor
    // layout + objects through interior walls; actors are drawn on top by the caller). (#104)
    void render(const Mat4& view, const Mat4& proj, double time = 0.0,
                const Vec3& camPos = Vec3{0, 0, 0}, const Vec3& playerPos = Vec3{0, 0, 0},
                float worldFade = 1.0f) const;

    const MapData& data() const { return map_; }
    Vec3 center() const { return center_; }
    float radius() const { return radius_; }
    // World Y of the ground vertex nearest to (wx,wz); centre height if no mesh.
    float heightAt(float wx, float wz) const;
    // Environment light colour (0..1 rgb) at the ground cell under (wx,wz): RSW ambient + diffuse
    // modulated by the baked shadow there. Used to tint actor sprites so a char/mob standing in a
    // building's shadow is dimmed (S., #118). White (full light) if no lightmap.
    Vec3 lightAt(float wx, float wz) const;

private:
    MapData map_;
    bool ready_ = false;
    bgfx::VertexBufferHandle vbh_ = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle ibh_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sampler_ = BGFX_INVALID_HANDLE;
    std::vector<bgfx::TextureHandle> textures_;  // parallel to map_.textures
    bgfx::TextureHandle white_ = BGFX_INVALID_HANDLE;
    // Optional per-tile normal maps (<tex>_n.png), parallel to textures_; flatNrm_ (a
    // 128,128,255 = tangent-space "up" texel) stands in where a tile ships no _n.png so the
    // ground shader always has a valid slot-2 binding and renders relief-free. (#107)
    std::vector<bgfx::TextureHandle> normalTextures_;
    bgfx::TextureHandle flatNrm_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle nrmSampler_ = BGFX_INVALID_HANDLE;  // s_nrm (slot 2)
    bgfx::UniformHandle nrmParams_ = BGFX_INVALID_HANDLE;   // u_nrmParams (has-map, strength)
    bgfx::UniformHandle lightDir_ = BGFX_INVALID_HANDLE;    // u_lightDir (RSW sun direction)
    // Per-pixel lightmap (baked shadows + coloured light) sampled by the ground shader.
    bgfx::TextureHandle lightmap_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle lightmapSampler_ = BGFX_INVALID_HANDLE;  // s_lightmap (slot 1)
    bgfx::UniformHandle mapDim_ = BGFX_INVALID_HANDLE;           // u_mapDim (cells -> UV)
    bgfx::UniformHandle ambient_ = BGFX_INVALID_HANDLE;          // u_ambient (RSW)
    bgfx::UniformHandle diffuse_ = BGFX_INVALID_HANDLE;          // u_diffuse (RSW)
    bgfx::UniformHandle fade_ = BGFX_INVALID_HANDLE;            // u_fade (ground x-ray opacity, #104)
    std::vector<Vec3> cellLight_;  // per-cell env light (W*H), for tinting actors in shadow (#118)
    ModelRenderer models_;  // RSM building/object meshes
    WaterRenderer water_;   // animated water surface
    Vec3 center_{0, 0, 0};
    float radius_ = 100.0f;
};

} // namespace uaro

#pragma once
#include <bgfx/bgfx.h>

#include <vector>

#include "core/Types.hpp"

namespace uaro {

class Application;
struct MapData;

// Scatters grass tufts on the ground cells whose top texture is "grassy" (green-dominant), the way
// roBrowser dresses fields (#grass). Each tuft is a cross of two world-vertical billboards textured
// with a procedural grass-blade sprite, alpha-cutout, tinted by nothing (the sprite is green). Height
// is a fraction of character height driven by the Normals level (g_grassHeightFrac); the on/off toggle
// is g_grassEnabled. Reuses the sprite3d shaders (no dedicated program). Owned by MapRenderer, drawn
// after the ground.
class GrassRenderer {
public:
    // hasWater: the map actually draws water (WaterRenderer built a surface) -> skip under-water cells.
    // When false the water level is meaningless, so the under-water skip is not applied.
    bool load(Application& app, const MapData& map, bool hasWater);
    void destroy();
    bool ready() const { return ready_; }

    // Draws the grass on view 0. Rebuilds the mesh first if the height fraction changed; skips entirely
    // when g_grassEnabled is false. When LOD is on (g_lodRadius > 0) only the chunks within that radius
    // of the player (playerX/playerZ, world XZ) are drawn (#lod).
    void render(double time, float playerX, float playerZ);

private:
    struct Clump {
        float x, z, groundY;  // world position (X already mirrored) + ground height
        u32 abgr;             // per-tuft tint = the cell tile's mean colour
        u8 model;             // which of the 5 tuft variants in the atlas (0..4)
    };
    // A spatial block of tufts -> one index-buffer range, drawn only when within the LOD radius (#lod).
    struct Chunk {
        float cx, cz, radius;   // bounding circle (world XZ)
        u32 idxStart, idxCount; // range into the shared index buffer
    };
    void buildMesh(float heightFrac);  // (re)upload the cross-quad mesh at the given height fraction

    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;  // vs_grass + fs_sprite3d
    bgfx::UniformHandle sampler_ = BGFX_INVALID_HANDLE;   // s_tex
    bgfx::UniformHandle fade_ = BGFX_INVALID_HANDLE;      // u_spriteFade (.x = 1)
    bgfx::UniformHandle bias_ = BGFX_INVALID_HANDLE;      // u_spriteBias (0)
    bgfx::UniformHandle wind_ = BGFX_INVALID_HANDLE;      // u_wind (xy dir, z time, w amplitude)
    bgfx::TextureHandle grassTex_ = BGFX_INVALID_HANDLE;  // procedural grass-blade sprite
    bgfx::VertexBufferHandle vbh_ = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle ibh_ = BGFX_INVALID_HANDLE;
    std::vector<Clump> clumps_;  // generated at load (chunk-sorted); mesh rebuilt from these on a height change
    std::vector<Chunk> chunks_;  // index ranges for per-frame LOD-radius culling
    u32 quadCount_ = 0;          // tuft crosses uploaded (2 quads each)
    float builtHeight_ = -1.0f;  // height fraction the current VB was built at
    bool ready_ = false;
};

} // namespace uaro

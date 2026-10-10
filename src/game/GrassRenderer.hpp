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
    // when g_grassEnabled is false.
    void render(double time);

private:
    struct Clump {
        float x, z, groundY;  // world position (X already mirrored) + ground height
        u32 abgr;             // per-tuft tint (currently white; the sprite carries the colour)
    };
    void buildMesh(float heightFrac);  // (re)upload the cross-quad mesh at the given height fraction

    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;  // vs_sprite3d + fs_sprite3d
    bgfx::UniformHandle sampler_ = BGFX_INVALID_HANDLE;   // s_tex
    bgfx::UniformHandle fade_ = BGFX_INVALID_HANDLE;      // u_spriteFade (.x = 1)
    bgfx::UniformHandle bias_ = BGFX_INVALID_HANDLE;      // u_spriteBias (0)
    bgfx::TextureHandle grassTex_ = BGFX_INVALID_HANDLE;  // procedural grass-blade sprite
    bgfx::VertexBufferHandle vbh_ = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle ibh_ = BGFX_INVALID_HANDLE;
    std::vector<Clump> clumps_;  // generated at load; mesh rebuilt from these on a height change
    u32 quadCount_ = 0;          // tuft crosses uploaded (2 quads each)
    float builtHeight_ = -1.0f;  // height fraction the current VB was built at
    bool ready_ = false;
};

} // namespace uaro

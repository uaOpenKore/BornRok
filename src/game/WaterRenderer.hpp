#pragma once
#include <bgfx/bgfx.h>

#include <vector>

#include "core/Types.hpp"

namespace uaro {

class Application;
struct MapData;

// Renders a map's animated water surface: one translucent plane at the RSW water
// level spanning the map, textured with the cycling data/texture/<워터>/water<type>NN.jpg
// frames. Drawn with the model vertex shader + the sprite-fade fragment shader (which
// gives the constant translucency), depth-tested against the already-drawn terrain so
// the water only shows where the ground dips below the water level (canals, ponds,
// flooded dungeon floors). Owned by MapRenderer, rendered after the ground and models.
class WaterRenderer {
public:
    bool load(Application& app, const MapData& map);
    void destroy();
    bool ready() const { return ready_; }

    // Submits the water quad on view 0 with the animation frame chosen from `time`.
    void render(double time) const;

private:
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;  // vs_model + fs_sprite3d
    bgfx::UniformHandle sampler_ = BGFX_INVALID_HANDLE;   // s_tex
    bgfx::UniformHandle fade_ = BGFX_INVALID_HANDLE;      // u_spriteFade (.x = alpha)
    bgfx::UniformHandle bias_ = BGFX_INVALID_HANDLE;      // u_spriteBias (kept 0 for the flat plane)
    bgfx::VertexBufferHandle vbh_ = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle ibh_ = BGFX_INVALID_HANDLE;
    std::vector<bgfx::TextureHandle> frames_;  // 32 animated water textures
    int animSpeed_ = 3;                         // RSW water anim speed (frame pacing)
    bool ready_ = false;
};

} // namespace uaro

#pragma once
#include <bgfx/bgfx.h>

#include "core/Types.hpp"

namespace uaro {

class Application;

// RO's flat ground shadow (data/sprite/shadow.spr): a translucent dark oval laid on
// the terrain under a character's feet. Mirrors WaterRenderer — a horizontal quad on
// the XZ plane drawn with vs_model + fs_sprite3d, depth-tested (LEQUAL) against the
// already-drawn terrain but NOT depth-writing, so it lies on the ground beneath the
// camera-facing character billboards (which are drawn afterwards). One shared texture;
// the per-actor quad is a transient buffer submitted each frame at the feet position.
class ShadowRenderer {
public:
    bool load(Application& app);
    void destroy();
    bool ready() const { return ready_; }

    // Submit one shadow oval centered on the ground at world (cx, groundY, cz). `scale`
    // enlarges it for bigger actors (1.0 = a normal character).
    void draw(bgfx::ViewId view, float cx, float groundY, float cz, float scale = 1.0f) const;

private:
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;  // vs_sprite3d + fs_sprite3d
    bgfx::UniformHandle sampler_ = BGFX_INVALID_HANDLE;   // s_tex
    bgfx::UniformHandle fade_ = BGFX_INVALID_HANDLE;      // u_spriteFade (.x = alpha)
    bgfx::UniformHandle bias_ = BGFX_INVALID_HANDLE;      // u_spriteBias (kept 0; flat on the ground)
    bgfx::TextureHandle tex_ = BGFX_INVALID_HANDLE;       // shadow.spr frame 0, RGBA
    bgfx::VertexLayout layout_;
    float halfW_ = 0.2f;  // world half-extents derived from the 35x17 sprite
    float halfD_ = 0.16f;
    bool ready_ = false;
};

} // namespace uaro

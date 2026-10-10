#pragma once
#include <bgfx/bgfx.h>

#include <vector>

#include "core/Types.hpp"
#include "core/math/Math.hpp"

namespace uaro {

class Application;
struct MapData;

// Per-frame water reflection inputs (#water), filled by GameScene from RenderDevice. mode 0 = off
// (classic water), 1 = light (sky tint only), 2 = ssr / 3 = planar (sample reflectTex rendered from
// the mirror camera). For modes 2/3 MapRenderer re-renders the terrain into the reflection view with
// `mirror`+`proj`; WaterRenderer samples reflectTex. Default = off, so the old call sites are unchanged.
struct ReflectionParams {
    int mode = 0;
    bgfx::ViewId view = 4;  // == RenderDevice::kReflectView (kept raw to avoid a render/ include here)
    Mat4 mirror;            // mirror-camera view matrix (reflected across the water plane)
    Mat4 proj;              // projection (same as the main camera)
    bgfx::TextureHandle reflectTex = BGFX_INVALID_HANDLE;
    int flipY = 0;              // 1 => flip the reflection UV's Y (render-target origin)
    float sky[3] = {0.45f, 0.58f, 0.78f};  // sky/ambient reflection colour (Light tier)
    float reflectivity = 0.5f;  // 0 = water only, 1 = full reflection
    float ripple = 0.02f;       // reflection UV distortion amount
};

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

    // Submits the water quad on view 0 with the animation frame chosen from `time`. `refl` selects the
    // reflection mode + supplies the reflection texture (modes 2/3); default = off (classic look).
    void render(double time, const ReflectionParams& refl = {}) const;

private:
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;  // vs_water + fs_water (or sprite3d fallback)
    bool newShader_ = false;  // true if the dedicated water program loaded (reflections available)
    bgfx::UniformHandle sampler_ = BGFX_INVALID_HANDLE;   // s_tex (slot 0)
    bgfx::UniformHandle reflSampler_ = BGFX_INVALID_HANDLE;  // s_refl (slot 1, reflection colour)
    bgfx::UniformHandle fade_ = BGFX_INVALID_HANDLE;      // u_spriteFade (.x = alpha)
    bgfx::UniformHandle bias_ = BGFX_INVALID_HANDLE;      // u_spriteBias (sprite3d fallback only, kept 0)
    bgfx::UniformHandle reflParams_ = BGFX_INVALID_HANDLE;  // u_waterRefl (mode, reflectivity, flipY, ripple)
    bgfx::UniformHandle sky_ = BGFX_INVALID_HANDLE;        // u_waterSky (rgb sky/ambient reflection colour)
    bgfx::VertexBufferHandle vbh_ = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle ibh_ = BGFX_INVALID_HANDLE;
    std::vector<bgfx::TextureHandle> frames_;  // 32 animated water textures
    int animSpeed_ = 3;                         // RSW water anim speed (frame pacing)
    bool ready_ = false;
};

} // namespace uaro

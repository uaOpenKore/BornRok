#pragma once
#include <bgfx/bgfx.h>

#include <vector>

#include "core/math/Math.hpp"

namespace uaro {

class Application;

// Volumetric light "glow" for dungeon point lights (#117 part B). Each RSW point light
// (data/*.rsw object type 2 -> RswLightSource) is drawn as an additive, camera-facing radial
// glow billboard tinted by its colour and sized by its range — an inexpensive volumetric-haze
// look around torches/lamps that needs no scene depth (a true raymarched cone would; see part
// B v2). Gated by the same "God Rays" toggle as the sun shafts. All lights batch into one
// transient buffer + one additive submit per frame.
class LightGlowRenderer {
public:
    bool load(Application& app);
    void destroy();
    bool ready() const { return ready_; }

    struct Glow {
        Vec3 pos;              // world-space light position
        float r, g, b;         // 0..1 tint
        float halfSize;        // world half-extent of the glow quad
    };
    void setLights(std::vector<Glow> lights) { lights_ = std::move(lights); }
    void clear() { lights_.clear(); }
    bool empty() const { return lights_.empty(); }
    const std::vector<Glow>& lights() const { return lights_; }  // for the Rays mode screen projection

    // Draw every glow as an additive camera-facing billboard on `view`. right/up = the camera axes
    // in world space (view matrix rows), same as the actor billboards. `extras` (optional, count =
    // extraCount) draws that many MORE glows on top of the map's lights — used for the outdoor sun,
    // built from 2 layers (a small bright yellow core + a wide dim halo) so it reads as a disc.
    void render(bgfx::ViewId view, const Vec3& right, const Vec3& up, const Glow* extras = nullptr,
                u32 extraCount = 0) const;

private:
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;  // vs_glow + fs_glow
    bgfx::VertexLayout layout_;
    std::vector<Glow> lights_;
    bool ready_ = false;
};

}  // namespace uaro

#pragma once
#include "core/Types.hpp"

namespace uaro {

// Texture-filter modes chosen in Setup Video (#104), read by the ground (world) and actor-sprite
// (objects) draw paths to override their bgfx sampler at draw time. 0 = disabled (nearest/point,
// crisp pixels), 1 = bilinear, 2 = trilinear. Without mipmaps (RO sprites/ground have none) 1 and 2
// are visually the same linear filter; the distinction is kept for the UI and future mipmapped art.
// Defaults match the Setup Video defaults: world = trilinear, objects = bilinear.
inline int g_worldFilterMode = 2;
inline int g_objectFilterMode = 1;

// Normals toggle (Settings -> Video -> Normals: Off/x1/x1.5/x2). The luminance normal maps
// are GENERATED at the x2 base; this factor scales the bump in-shader (u_nrmParams.y /
// u_spriteLight.w), so switching is instant with no texture regeneration. 0 = off,
// 0.5 = x1, 0.75 = x1.5 (default on real GPUs), 1.0 = x2.
inline float g_normalsFactor = 0.75f;

}  // namespace uaro

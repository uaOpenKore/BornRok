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

// Grass (#grass, Settings -> Video). g_grassEnabled toggles the ground-grass billboards; the Normals
// level drives their HEIGHT as a fraction of character height (S.: x1 = 20%, x1.5 = 30%, x2 = 40%).
// Read by GrassRenderer each frame; a height change triggers a cheap mesh rebuild.
inline bool g_grassEnabled = true;
inline float g_grassHeightFrac = 0.20f;  // 0.20 / 0.30 / 0.40

// Grass wind sway (#grasswind, Settings -> Video). Sways the grass blade tips with one slow world wind
// (vs_grass). Default on; off holds the blades still (amplitude 0) with no other cost. Read by
// GrassRenderer each frame.
inline bool g_grassWind = true;

// Grass flowers (#grassflowers, Settings -> Video). Scatters bright flower heads through the grass (a
// subset of tufts get a coloured tip). Default on; a change triggers a cheap grass-mesh rebuild.
inline bool g_grassFlowers = true;

// Ambient / weather particle toggles (Settings -> Video). Each gates one family of coded ambient
// particles in GameScene::updateAmbientFx; all default on. Off simply stops that family emitting (the
// already-live particles finish their life), so toggling is instant and costs nothing when off.
inline bool g_fxWeather = true;    // server weather fall: rain / snow / sakura / leaves
inline bool g_fxFireflies = true;  // wandering warm fireflies (map emitter id 45)
inline bool g_fxSmoke = true;      // chimney smoke puffs (map emitter id 44)
inline bool g_fxSparkle = true;    // white sparkle glints (map emitter id 165)

// Animated water surface ripple (#waterripples, Settings -> Video). A tiny time-driven UV wobble so the
// water shimmers; applies in all reflection modes. Default on; off is byte-identical to the classic look.
inline bool g_waterRipples = true;

// LOD (#lod, Settings -> Video, for Switch/Android perf). 0 = off (draw every RSM object); else the
// world-unit RADIUS around the player within which objects are drawn. With LOD on, objects in the rear
// 180° (behind the player relative to the camera's view direction) are also skipped. Read by
// ModelRenderer each frame.
inline float g_lodRadius = 0.0f;  // 0 off / 25 / 40 / 60 / 80

}  // namespace uaro

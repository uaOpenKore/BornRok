#pragma once
#include <string>
#include <vector>

#include "core/Types.hpp"

namespace uaro {

// Parsed subset of a Unity ParticleSystem (class 198) + its renderer (class 199), enough to
// re-emit the effect on our CPU particle engine (game/Particles.hpp). RoM ships stripped type
// trees (Unity 2021.3.21f1), so the payload is read positionally in AssetStudio's field order —
// modules are variable-length (curves carry a keyframe list), so the whole object MUST be read
// sequentially from the start; fixed offsets do not work across effects.
//
// We keep only what drives a recognisable billboard burst: emission timing, per-particle
// lifetime/speed/size ranges, the start colour, gravity, and the emitter shape. Material/texture
// resolution (the effect's own sprite) lives in 23k external bundles behind hash refs and is out
// of scope — we render with the soft glow tinted by startColor.

// A Unity MinMaxCurve resolved to a scalar range. state: 0 constant, 1 curve, 2 twoConstants,
// 3 twoCurves. We collapse curves to [min,max] of their sampled control values * scalar.
struct UnityMinMax {
    float lo = 0.0f;
    float hi = 0.0f;
    float mid() const { return 0.5f * (lo + hi); }
    // Sample a pseudo-value in [lo,hi] from a 0..1 fraction (deterministic spread, no RNG here).
    float at(float t) const { return lo + (hi - lo) * t; }
};

struct UnityParticleDesc {
    bool ok = false;

    // System header
    float duration = 1.0f;   // lengthInSec
    bool looping = false;
    float simulationSpeed = 1.0f;

    // InitialModule
    UnityMinMax startLifetime{1.0f, 1.0f};
    UnityMinMax startSpeed{1.0f, 1.0f};
    UnityMinMax startSize{1.0f, 1.0f};
    float startColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};  // resolved constant / gradient start
    float gravityModifier = 0.0f;
    i32 maxParticles = 1000;

    // EmissionModule
    bool emissionEnabled = true;
    float rateOverTime = 10.0f;
    struct Burst {
        float time = 0.0f;
        i32 count = 0;
    };
    std::vector<Burst> bursts;

    // ShapeModule (mapped to our emitter): 0 sphere, 1 hemisphere, 2 cone, 3 box, 4 circle, 5 edge.
    bool shapeEnabled = true;
    i32 shapeKind = 0;
    float shapeRadius = 0.5f;
    float shapeAngleRad = 0.436f;  // ~25 deg default cone half-angle

    // Renderer (class 199): 0 billboard, 1 stretch, 4 mesh, 5 none.
    i32 renderMode = 0;

    // MinMaxGradient block placement for this Unity version (2=gradients before colour pair,
    // 3=after). Pinned empirically against real 2021.3 bundles; the parser reads it from here.
    i32 gradientPlacement = 3;
};

// Parse a ParticleSystem object payload (raw bytes of the class-198 object, little-endian).
// `unityVersion` is the SerializedFile's version string ("2021.3.21f1"); it selects the field
// layout. Returns false (out.ok stays false) if the header does not look like a ParticleSystem.
bool parseUnityParticleSystem(const u8* data, usize size, const std::string& unityVersion,
                              UnityParticleDesc& out);

// Fold the class-199 renderer payload's render mode into `out` (optional; billboard if absent).
void parseUnityParticleRenderer(const u8* data, usize size, const std::string& unityVersion,
                                UnityParticleDesc& out);

}  // namespace uaro

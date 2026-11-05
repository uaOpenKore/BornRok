#pragma once
#include "core/math/Math.hpp"
#include "render/Texture.hpp"

namespace uaro {

class Vfs;
struct WorldSpritePass;

// Geometric primitive skill effects — procedurally built world geometry (ribbons/rings), as opposed
// to StrEffect (which plays a parsed .str billboard animation). These reproduce the CLASSIC effects
// the original exe draws with its CEffect primitive system and that have NO .str in the content, so
// StrEffect/effectFxStr renders nothing for them. Ported from si-klyde/nostalre (Apache-2.0), whose
// lib/effects reverse-engineers the per-frame math of these effects straight from the game.
//
// PILOT: EF_WIND (effect id 224). If this layer proves out, the other ~70 classic coded effects
// (volcano, grandcross, heavensdrive, defender, ...) follow the same pattern.

// EF_WIND (224) — Wizard's "Wind" tornado/funnel: four nested, expanding, spinning partial-arc cloud
// ribbons (cloud11.tga) sitting above the caster. Faithful port of nostalre's wind.rs + radial_emitter.rs.
class WindEffect {
public:
    static constexpr int kSlots = 4;      // RADIAL_EMITTER_SLOTS
    static constexpr int kDivision = 21;  // RADIAL_EMITTER_DIVISION (segments = kDivision-1)

    WindEffect(const Vec3& worldPos, double bornTime);

    // Advance the simulation to absolute time `now` (seconds). Monotonic: integrates whole 60fps
    // frames up to now, so calling it every render frame reproduces the original per-frame tick.
    void advance(double now);
    // True once the effect has run its full 300 frames (5 s @ 60 fps).
    bool finished(double now) const;

    // Draw the four ribbons at `base` (the caster's live world position, so the funnel follows them).
    // `tex` is cloud11.tga, loaded once by the owner via loadEffectTexture().
    void render(const WorldSpritePass& pass, const Texture& tex, const Vec3& base) const;

private:
    struct Slot {
        bool alive = false;
        unsigned process = 0;
        float distance = 0.0f;
        float rotStartDeg = 0.0f;
        float alphaB = 0.0f;
        float fullArcDeg = 0.0f;
        float height[kDivision] = {0.0f};
    };
    void integrateTo(unsigned targetFrame);

    double born_ = 0.0;
    float ageFrames_ = 0.0f;
    unsigned lastFrame_ = 0;
    Slot slots_[kSlots];
};

// EF_TURNUNDEAD (82) — Priest Turn Undead holy burst at the target: a two-phase effect. Phase 1
// (frames 0-20): a shrinking halo disc + 5 shrinking additive flash rays radiating from the centre.
// Phase 2 (frames 50-80): an expanding flat ground ring. Faithful port of nostalre turnundead.rs +
// spike_burst.rs. Needs three effect textures: alpha_down.tga (halo), alpha_center.tga (rays),
// ring_liner.tga (ring).
class TurnUndeadEffect {
public:
    static constexpr int kSpikes = 5;

    TurnUndeadEffect(const Vec3& worldPos, double bornTime);
    void advance(double now);
    bool finished(double now) const;
    // `halo`/`spike`/`ring` are the three effect textures, loaded once by the owner.
    void render(const WorldSpritePass& pass, const Texture& halo, const Texture& spike,
                const Texture& ring, const Vec3& base) const;

private:
    struct Spike {
        float longitude0 = 0.0f;   // initial angle (rad)
        float angSpeed = 0.0f;     // rad/frame (already sign-flipped to screen CCW)
        float lenInit = 0.0f;      // spawn length (RO-native units)
        float growth = 0.0f;       // length change per frame
    };
    double born_ = 0.0;
    float ageFrames_ = 0.0f;
    Spike spikes_[kSpikes];
};

// Load data/texture/effect/<nameTga> into `out` through the same decode + magenta-key path StrEffect
// uses for its layers (hi-res .png override honoured). Returns false if the file is missing/undecodable;
// the caller then simply skips the effect (safe no-op, never a crash).
bool loadEffectTexture(const Vfs& vfs, const char* nameTga, Texture& out);

}  // namespace uaro

#pragma once
#include <vector>

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

// EF_BLESSING (AL_BLESSING, skill 34) — Priest Blessing: a cyan holy ground disc under the target plus
// rising 축복 (blessing) angel sprites and particle6 twinkles floating up around them. Faithful port of
// nostalre blessing.rs for the SIMULATION (spawn cadence, counts, fade curves), but the spatial
// magnitudes are expressed directly in WORLD units (not kUnit*RO) so an over-head buff sits at a sane
// height for our scale. This layer only owns the simulation + the ground disc (it has WorldSpritePass +
// Texture); the angel/twinkle .spr sprites are drawn by the owner (GameScene, via CharacterActor), which
// reads angels()/twinkles() each frame. Replaces the older coded cherub-orbit Blessing.
class BlessingEffect {
public:
    BlessingEffect(const Vec3& worldPos, double bornTime);
    void advance(double now);
    bool finished(double now) const;
    // Draw the cyan ground disc (alpha_down.tga) at `base` (the buffed unit's live feet position).
    void renderDisc(const WorldSpritePass& pass, const Texture& discTex, const Vec3& base) const;

    struct Angel {
        double born = 0.0;    // absolute spawn time -> sprite animStart, so each angel plays from birth
        float alpha = 0.0f;   // current alpha (0 = skip)
        float spawnFrame = 0.0f;
    };
    struct Twinkle {
        Vec3 offset{};        // current position relative to `base`, WORLD units
        float alpha = 0.0f;
        double born = 0.0;
        float x = 0.0f, z = 0.0f, y0 = 0.0f, rise = 0.0f, bornFrame = 0.0f;  // sim state
    };
    const std::vector<Angel>& angels() const { return angels_; }
    const std::vector<Twinkle>& twinkles() const { return twinkles_; }
    float angelHeight() const;   // world-units Y of the angel anchor above `base`

private:
    unsigned lcg();
    float lcgFloat();
    void spawnTwinkle(float bornFrame, double now);

    double born_ = 0.0;
    float ageFrames_ = 0.0f;
    int lastSpawnFrame_ = -1;
    unsigned rng_ = 0;
    std::vector<Angel> angels_;
    std::vector<Twinkle> twinkles_;
};

// EF_AGIUP (AL_INCAGI, skill 29) — Increase Agility cast fountain: paired perpendicular vertical streaks
// (ac_center2.tga) rising around the unit, additive, blue/yellow alternating. Faithful port of nostalre
// agiup.rs — pure geometry (no .spr). A streak spawns every 2 frames, each lives 50 frames, total 100
// frames (~1.7 s). Particles are derived deterministically from their index (hash), so nothing is stored.
class AgiUpEffect {
public:
    AgiUpEffect(const Vec3& worldPos, double bornTime);
    void advance(double now);
    bool finished(double now) const;
    void render(const WorldSpritePass& pass, const Texture& tex, const Vec3& base) const;

private:
    double born_ = 0.0;
    float ageFrames_ = 0.0f;
};

// EF_HEAL (AL_HEAL, skill 28) — Priest Heal: two green radial ribbon rings (alpha_down.tga, additive)
// rising from the ground around the target, plus rising green motes (pok1.tga). Faithful port of
// nostalre heal.rs (HEAL params) + particle_up.rs (HEAL_MOTE). Pure geometry (no .spr). Only the
// flag1==0 RiseLaw::Heal ring path and the no-body-flash mote path are ported — all that AL_HEAL uses.
class HealEffect {
public:
    static constexpr int kDivision = 21;  // RADIAL_EMITTER_DIVISION
    static constexpr int kSlots = 2;

    HealEffect(const Vec3& worldPos, double bornTime);
    void advance(double now);
    bool finished(double now) const;
    void render(const WorldSpritePass& pass, const Texture& ringTex, const Texture& moteTex,
                const Vec3& base) const;

private:
    struct Ring {
        int ec = 0;
        float distance = 0.0f, maxHeight = 0.0f, rotStartDeg = 0.0f;
        float alpha = 0.0f, alphaT = 0.0f;
        unsigned process = 0;
        float heights[kDivision] = {0.0f};
    };
    struct Mote {
        float x0 = 0.0f, z0 = 0.0f;  // spawn xz offset from base (world units)
        float size = 0.0f, rise = 0.0f, alpha = 0.0f;
        int process = 0;
    };
    void integrateTo(int targetFrame);
    unsigned moteRng();
    float moteRand(float lo, float hi);

    double born_ = 0.0;
    float ageFrames_ = 0.0f;
    int lastFrame_ = 0;
    unsigned rng_ = 0;
    Ring rings_[kSlots];
    std::vector<Mote> motes_;
};

// EF_NAPALMBEAT (MG_NAPALMBEAT, skill 11) — a cluster of additive explosion puffs bursting around the
// target, each cycling the 8-frame explosion sprite (폭발1-8.tga). Faithful port of nostalre
// napalmbeat.rs: 15 puffs, one spawns per 60fps frame, each lives 30 frames; its radial offset, ring
// radius and size are deterministic from a world-position seed (so the cluster is stable per cast).
// Pure geometry over the 8 explosion textures (no .spr/.str). Replaces the old coded particle blob.
class NapalmBeatEffect {
public:
    static constexpr int kCount = 15;
    static constexpr int kTexFrames = 8;

    NapalmBeatEffect(const Vec3& worldPos, double bornTime);
    void advance(double now);
    bool finished(double now) const;
    // `tex` are the 8 explosion frames (폭발1..8.tga), loaded once by the owner (null slot -> skipped).
    // `base` is the target's live feet position so the burst follows it.
    void render(const WorldSpritePass& pass, const Texture* const tex[kTexFrames], const Vec3& base) const;

private:
    struct Puff {
        float spawnFrame = 0.0f, ox = 0.0f, oz = 0.0f, size = 0.0f;
    };
    double born_ = 0.0;
    float ageFrames_ = 0.0f;
    Puff puffs_[kCount];
};

// EF_EARTHSPIKE (WZ_EARTHSPIKE, skill 90) — a central stone blade erupting from the cell with a damped-
// spring overshoot, surrounded by a ring of 6 outward-leaning blades that rise out of the ground.
// Faithful port of nostalre earthspike.rs + spike_util.rs ("QuadHorn" spike math). Stone texture. The
// shared QuadHorn blade draw (a base-anchored billboard blade leaning along a tilt/heading apex) is
// reused by Heaven's Drive and Frost Diver. Replaces skill 90's flat stone-particle scatter.
class EarthSpikeEffect {
public:
    static constexpr int kRing = 6;

    EarthSpikeEffect(const Vec3& worldPos, double bornTime);
    void advance(double now);
    bool finished(double now) const;
    // `stoneTex` = stone.bmp/.tga, loaded once by the owner. `base` = the cell's live world position.
    void render(const WorldSpritePass& pass, const Texture& stoneTex, const Vec3& base) const;

private:
    struct Spike {
        Vec3 base0{};       // base offset from the anchor, world units (before rise)
        Vec3 vel{};         // apex velocity, world units per frame (0 for the centre spike)
        Vec3 apex{};        // unit apex direction in world space (blade grows along this)
        float size = 0.0f;        // blade half-width, world units
        float restHeight = 0.0f;  // blade length at rest, world units
        bool vibrate = false;     // centre spike: height follows the spring envelope
    };
    double born_ = 0.0;
    float ageFrames_ = 0.0f;
    Spike spikes_[kRing + 1];
};

// EF_HEAVENSDRIVE (WZ_HEAVENDRIVE, skill 91) — a 5×5 grid of 25 stone blades that rise out of the ground
// across the skill's splash, hold, then sink back at the end of their life. Faithful port of nostalre
// heavensdrive.rs: all blades share one rise/hold/sink motion (speed+accel phases); each has a random
// size/height/heading/tilt and starts buried, poking out as it rises (depth-tested, so the terrain hides
// the buried part). Reuses the QuadHorn blade draw + stone texture. Replaces skill 91's particle grid.
class HeavensDriveEffect {
public:
    static constexpr int kGrid = 5;
    static constexpr int kCount = kGrid * kGrid;  // 25

    HeavensDriveEffect(const Vec3& worldPos, double bornTime);
    void advance(double now);
    bool finished(double now) const;
    void render(const WorldSpritePass& pass, const Texture& stoneTex, const Vec3& base) const;

private:
    struct Blade {
        Vec3 base0{};  // base offset from the cell, world units (buried below ground at spawn)
        Vec3 apex{};   // unit apex direction in world space (blade grows / rises along this)
        float size = 0.0f, height = 0.0f;
    };
    void integrateTo(float targetFrame);
    double born_ = 0.0;
    float ageFrames_ = 0.0f, lastFrame_ = 0.0f;
    float distance_ = 0.0f, speed_ = 0.0f, accel_ = 0.0f;  // shared native rise/sink motion
    Blade blades_[kCount];
};

// EF_FROSTDIVER (MG_FROSTDIVER, skill 15) — a burst of 3-5 additive ice blades erupting around the frozen
// target, staggered over the first 14 frames, each rising then fading over a 40-frame life. Faithful port
// of nostalre frost_diver.rs (FROSTDIVER params, burst mode). Ice texture, additive. Reuses the QuadHorn
// blade primitive. Replaces skill 15's coded ice-crystal shatter.
class FrostDiverEffect {
public:
    static constexpr int kMaxSpikes = 5;

    FrostDiverEffect(const Vec3& worldPos, double bornTime);
    void advance(double now);
    bool finished(double now) const;
    // `iceTex` = ice.tga, loaded once by the owner. `base` = the frozen target's live position.
    void render(const WorldSpritePass& pass, const Texture& iceTex, const Vec3& base) const;

private:
    struct Ice {
        Vec3 base0{};      // placement offset from the target, world units
        Vec3 vel{};        // apex velocity, world units per frame (rise)
        Vec3 apex{};       // unit apex direction in world space
        float size = 0.0f, height = 0.0f, spawnFrame = 0.0f;
    };
    double born_ = 0.0;
    float ageFrames_ = 0.0f;
    int count_ = 0;
    Ice spikes_[kMaxSpikes];
};

// EF_YUFITEL (WZ_JUPITEL, skill 84) — Jupitel Thunder's plasma ball: two co-travelling additive
// billboards (a static centre glow thunder_center.bmp + an animated 6-frame ball thunder_ball_a-f.bmp)
// flying from the caster to the target over 20 frames. Faithful port of nostalre yupitel.rs. Replaces
// skill 84's coded plasma stream.
class JupitelEffect {
public:
    static constexpr int kBallFrames = 6;
    static constexpr int kTexCount = 1 + kBallFrames;  // [0]=centre, [1..6]=ball a..f

    JupitelEffect(const Vec3& from, const Vec3& to, double bornTime);
    void advance(double now);
    bool finished(double now) const;
    // `tex[0]` = thunder_center, `tex[1..6]` = thunder_ball_a..f (null slot -> skipped).
    void render(const WorldSpritePass& pass, const Texture* const tex[kTexCount]) const;

private:
    Vec3 current() const;
    Vec3 from_{}, to_{}, vel_{};  // vel = world units per frame toward the target
    double born_ = 0.0;
    float ageFrames_ = 0.0f;
};

// Load data/texture/effect/<nameTga> into `out` through the same decode + magenta-key path StrEffect
// uses for its layers (hi-res .png override honoured). Returns false if the file is missing/undecodable;
// the caller then simply skips the effect (safe no-op, never a crash).
bool loadEffectTexture(const Vfs& vfs, const char* nameTga, Texture& out);

}  // namespace uaro

#pragma once
#include <cstddef>
#include <vector>

#include "core/math/Math.hpp"

namespace uaro {

// CPU particle engine for skill visual effects (heal sparks, magnum-break burst, level auras).
// Engine-agnostic: it only integrates each particle's motion/colour/size over time; the renderer
// draws every live particle as a camera-facing billboard (additive blend) using its pos/size/colour.
// Adapted from S.'s reference engine, with an object pool so a burst never allocates mid-frame.
struct Particle {
    Vec3 pos;
    Vec3 vel;
    Vec3 accel;             // per-second velocity change (e.g. gravity for falling debris)
    float r = 1, g = 1, b = 1, a = 1;       // colour, 0..1
    float dr = 0, dg = 0, db = 0, da = 0;   // per-second colour delta (da < 0 = fade out)
    float size = 1.0f;      // world units (billboard half-extent)
    float growth = 0.0f;    // per-second size change (+ = expanding smoke, - = shrinking spark)
    float life = 0.0f;      // remaining seconds
    bool alive = false;
    int fxTex = -1;         // index into the renderer's RoM effect-texture registry (-1 = soft glow)
    float stretch = 1.0f;   // >1 = billboard elongated along the velocity direction (beam/ray effects)
    // Orientation (S.: heal green rune circle "должен лежать на земле"). groundFlat = lay the billboard
    // FLAT on the world XZ ground plane (normal = +Y) instead of the default camera billboard, so a rune
    // circle reads as a disc on the floor. spin = roll rate (rad/s) about the billboard centre; for a
    // groundFlat disc it turns about the vertical axis (+ = clockwise seen from above). 0 = no roll.
    bool groundFlat = false;
    float spin = 0.0f;
    // Blend mode: the skill/ambient billboards are ADDITIVE (glow). Airship clouds are opaque-ish white
    // puffs over a blue sky, so they must ALPHA-blend instead (additive white would blow out to solid
    // white). true = draw this particle with src-alpha/one-minus-src-alpha. (roBrowser Sky.js)
    bool alphaBlend = false;
};

class ParticleSystem {
public:
    explicit ParticleSystem(std::size_t maxParticles = 256) : pool_(maxParticles) {}

    void setEmitter(const Vec3& p) { emitter_ = p; }
    const Vec3& emitter() const { return emitter_; }

    // Spawn one particle from a template at the emitter position, reusing a dead pool slot. If the
    // pool is full the spawn is dropped (older particles fade out shortly anyway). Returns true if a
    // slot was used. The caller fills the template's velocity/colour/life; pos is set to the emitter.
    bool emit(Particle p) {
        for (Particle& slot : pool_) {
            if (!slot.alive) {
                p.pos = emitter_;
                p.alive = true;
                slot = p;
                return true;
            }
        }
        return false;
    }

    // Advance every live particle by dt seconds: semi-implicit Euler motion, colour fade, grow, age.
    void update(float dt) {
        for (Particle& p : pool_) {
            if (!p.alive) continue;
            p.life -= dt;
            if (p.life <= 0.0f) {
                p.alive = false;
                continue;
            }
            p.vel = p.vel + p.accel * dt;
            p.pos = p.pos + p.vel * dt;
            p.r += p.dr * dt;
            p.g += p.dg * dt;
            p.b += p.db * dt;
            p.a += p.da * dt;
            if (p.a < 0.0f) p.a = 0.0f;
            p.size += p.growth * dt;
            if (p.size < 0.0f) p.size = 0.0f;
        }
    }

    bool anyAlive() const {
        for (const Particle& p : pool_)
            if (p.alive) return true;
        return false;
    }
    std::size_t aliveCount() const {
        std::size_t n = 0;
        for (const Particle& p : pool_)
            if (p.alive) ++n;
        return n;
    }
    const std::vector<Particle>& particles() const { return pool_; }  // includes dead slots; check .alive
    void clear() {
        for (Particle& p : pool_) p.alive = false;
    }

private:
    std::vector<Particle> pool_;
    Vec3 emitter_;
};

}  // namespace uaro

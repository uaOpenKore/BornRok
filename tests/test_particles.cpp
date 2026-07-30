#include "game/Particles.hpp"

#include "microtest.hpp"

using namespace uaro;

TEST_CASE(particles_emit_at_emitter_and_pool_is_finite) {
    ParticleSystem ps(4);
    CHECK(!ps.anyAlive());
    ps.setEmitter({10, 0, 5});
    Particle t;
    t.life = 1.0f;
    CHECK(ps.emit(t));
    CHECK_EQ(ps.aliveCount(), 1u);
    CHECK_EQ(ps.particles()[0].pos.x, 10.0f);  // spawned at the emitter
    CHECK_EQ(ps.particles()[0].pos.z, 5.0f);
    ps.emit(t);
    ps.emit(t);
    ps.emit(t);  // pool now full (4)
    CHECK_EQ(ps.aliveCount(), 4u);
    CHECK(!ps.emit(t));  // full -> dropped
}

TEST_CASE(particles_update_motion_fade_and_life) {
    ParticleSystem ps(2);
    Particle t;
    t.life = 1.0f;
    t.vel = {0, 2, 0};   // rises
    t.da = -1.0f;        // fades over 1s
    t.size = 1.0f;
    t.growth = -0.5f;
    ps.emit(t);
    ps.update(0.5f);
    const Particle& p = ps.particles()[0];
    CHECK(p.alive);
    CHECK_EQ(p.pos.y, 1.0f);                    // 2 * 0.5
    CHECK(p.a > 0.49f && p.a < 0.51f);          // ~0.5
    CHECK(p.size > 0.74f && p.size < 0.76f);    // ~0.75
    ps.update(0.6f);                            // total 1.1s > life -> dead
    CHECK(!ps.particles()[0].alive);
    CHECK(!ps.anyAlive());
}

TEST_CASE(particles_accel_applies_before_motion) {
    ParticleSystem ps(1);
    Particle t;
    t.life = 2.0f;
    t.accel = {0, -10, 0};  // gravity
    ps.emit(t);
    ps.update(1.0f);
    const Particle& p = ps.particles()[0];
    CHECK_EQ(p.vel.y, -10.0f);  // accel integrated
    CHECK_EQ(p.pos.y, -10.0f);  // then position by the new velocity (semi-implicit Euler)
}

TEST_CASE(particles_clear_kills_all) {
    ParticleSystem ps(3);
    Particle t;
    t.life = 5.0f;
    ps.emit(t);
    ps.emit(t);
    CHECK_EQ(ps.aliveCount(), 2u);
    ps.clear();
    CHECK(!ps.anyAlive());
}

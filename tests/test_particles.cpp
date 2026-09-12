#include "core/rng.h"
#include "game/tuning.h"
#include "render/particles.h"
#include "test_harness.h"

#include <cmath>
#include <memory>

namespace {

int alive_count(const ParticleSystem& ps) {
  int n = 0;
  for (int i = 0; i < MAX_PARTICLES; ++i) {
    if (ps.pool[i].life > 0.0f) ++n;
  }
  return n;
}

int visible_count(const ParticleSystem& ps) {
  int n = 0;
  for (int i = 0; i < MAX_PARTICLES; ++i) {
    if (ps.pool[i].life > 0.0f && ps.pool[i].delay <= 0.0f) ++n;
  }
  return n;
}

}  // namespace

// A tracer has to end at the surface it hit. Overshooting puts a visible streak
// through the wall; stopping short leaves it hanging in the air.
TEST(tracer_lands_on_the_impact_point) {
  auto ps = std::make_unique<ParticleSystem>();
  particles_init(*ps);
  const Vec3 start{0.0f, 1.0f, 0.0f};
  const Vec3 end{0.0f, 1.0f, -40.0f};
  const float speed = 420.0f;
  particles_tracer(*ps, start, end, {1.0f, 1.0f, 1.0f}, speed, 6, 0.04f);
  CHECK(alive_count(*ps) == 6);

  float travel = 40.0f / speed;
  float furthest = 0.0f;
  // Step past the moment the head should arrive and watch where they get to.
  for (int i = 0; i < 240; ++i) {
    particles_update(*ps, 1.0f / 240.0f);
    for (int k = 0; k < MAX_PARTICLES; ++k) {
      const Particle& p = ps->pool[k];
      if (p.life <= 0.0f || p.delay > 0.0f) continue;
      float travelled = vec3_length(p.pos - start);
      if (travelled > furthest) furthest = travelled;
    }
  }
  // Nothing may fly appreciably past the wall.
  CHECK(furthest < 41.0f);
  // And the streak must actually have got there, not fizzled halfway.
  CHECK(furthest > 38.0f);
  // Everything is gone shortly after arrival rather than lingering.
  CHECK_EQ_INT(alive_count(*ps), 0);
  CHECK(travel > 0.0f);
}

// The whole streak arrives together: each dot starts further back and is given
// exactly enough life to cover its own head start.
TEST(tracer_streak_arrives_together) {
  auto ps = std::make_unique<ParticleSystem>();
  particles_init(*ps);
  const Vec3 start{0.0f, 0.0f, 0.0f};
  const Vec3 end{60.0f, 0.0f, 0.0f};
  particles_tracer(*ps, start, end, {1.0f, 1.0f, 1.0f}, 300.0f, 6, 0.04f);

  float first_death = 1e30f;
  float last_death = 0.0f;
  for (int k = 0; k < MAX_PARTICLES; ++k) {
    const Particle& p = ps->pool[k];
    if (p.life <= 0.0f) continue;
    // Where each dot will be when its life runs out.
    Vec3 at_death = p.pos + p.vel * p.life;
    float d = vec3_length(at_death - end);
    if (d < first_death) first_death = d;
    if (d > last_death) last_death = d;
  }
  // Every dot expires at the impact point, within a few centimetres.
  CHECK(last_death < 0.25f);
  CHECK(first_death < 0.25f);
}

// A shotgun blast fires seven tracers at once. The old full-length tracer spent
// 252 of the 2048 particles on a single trigger pull, which starved explosions
// and sparks of slots; the travelling streak has to stay far below that.
TEST(shotgun_blast_stays_within_the_particle_budget) {
  auto ps = std::make_unique<ParticleSystem>();
  particles_init(*ps);
  Rng rng{0x1234ull};
  Vec3 start{0.0f, 1.0f, 0.0f};
  for (int pellet = 0; pellet < SHOTGUN_PELLETS; ++pellet) {
    Vec3 end{static_cast<float>(pellet) * 0.4f, 1.0f, -20.0f};
    particles_tracer(*ps, start, end, {1.0f, 0.9f, 0.6f}, 200.0f, 3, 0.034f);
    particles_impact(*ps, rng, end, {0.0f, 0.0f, -1.0f}, 0.1f);
  }
  int used = alive_count(*ps);
  CHECK(used < 80);                       // was 252 for the tracers alone
  CHECK(used > SHOTGUN_PELLETS);          // and it did actually draw something
  // Eight players all firing must not exhaust the pool.
  CHECK(used * 8 < MAX_PARTICLES);
}

// A delayed particle must be completely inert until its moment: not ageing, not
// moving, not drawn. Otherwise an impact drifts before it appears.
TEST(delayed_particles_stay_inert_until_due) {
  auto ps = std::make_unique<ParticleSystem>();
  particles_init(*ps);
  Rng rng{0x99ull};
  const Vec3 at{3.0f, 2.0f, 1.0f};
  particles_impact(*ps, rng, at, {0.0f, 0.0f, -1.0f}, 0.20f);
  int spawned = alive_count(*ps);
  CHECK(spawned > 0);
  CHECK_EQ_INT(visible_count(*ps), 0);  // nothing on screen yet

  // Halfway through the delay: still nothing visible, and nothing has moved.
  for (int i = 0; i < 6; ++i) particles_update(*ps, 1.0f / 60.0f);
  CHECK_EQ_INT(visible_count(*ps), 0);
  for (int k = 0; k < MAX_PARTICLES; ++k) {
    const Particle& p = ps->pool[k];
    if (p.life <= 0.0f) continue;
    CHECK_NEAR(vec3_length(p.pos - at), 0.0f, 0.0001f);
  }

  // Past the delay it appears, and only then starts moving.
  for (int i = 0; i < 12; ++i) particles_update(*ps, 1.0f / 60.0f);
  CHECK(visible_count(*ps) > 0);
  bool moved = false;
  for (int k = 0; k < MAX_PARTICLES; ++k) {
    const Particle& p = ps->pool[k];
    if (p.life <= 0.0f || p.delay > 0.0f) continue;
    if (vec3_length(p.pos - at) > 0.001f) moved = true;
  }
  CHECK(moved);
}

// A zero-length tracer (muzzle already inside a wall) must not divide by zero
// or spray particles at the origin.
TEST(tracer_ignores_a_degenerate_shot) {
  auto ps = std::make_unique<ParticleSystem>();
  particles_init(*ps);
  particles_tracer(*ps, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {1, 1, 1}, 400.0f, 6, 0.04f);
  CHECK_EQ_INT(alive_count(*ps), 0);
  // And a zero speed must not hang or produce infinite lifetimes.
  particles_tracer(*ps, {0, 0, 0}, {0, 0, -10.0f}, {1, 1, 1}, 0.0f, 4, 0.04f);
  for (int k = 0; k < MAX_PARTICLES; ++k) {
    CHECK(std::isfinite(ps->pool[k].life));
  }
}

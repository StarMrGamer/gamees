#pragma once

#include "core/rng.h"
#include "render/renderer.h"

constexpr int MAX_PARTICLES = 2048;

struct Particle {
  Vec3 pos, vel;
  Vec3 color;
  float life, max_life, size;
  bool gravity;
  // Seconds before the particle appears or moves. Added last so the existing
  // aggregate initialisers keep working and default it to zero. It exists so an
  // impact can land exactly when the tracer reaches the wall rather than a few
  // frames early.
  float delay;
};

struct ParticleSystem {
  Particle pool[MAX_PARTICLES];
  int alive;
  int spawn_cursor;  // rotating free-slot hint; avoids rescanning from 0
};

void particles_init(ParticleSystem& ps);
void particles_update(ParticleSystem& ps, float dt);
void particles_render(ParticleSystem& ps, Renderer& r, const Camera& cam);
void particles_explosion(ParticleSystem& ps, Rng& rng, Vec3 pos);
void particles_sparks(ParticleSystem& ps, Rng& rng, Vec3 pos, Vec3 normal);
void particles_muzzle_flash(ParticleSystem& ps, Rng& rng, Vec3 pos, Vec3 dir);
void particles_trail(ParticleSystem& ps, Rng& rng, Vec3 pos);
// Emits a continuous effect at a fixed rate per second, independent of the
// frame rate. `accum` is the caller's per-emitter carry.
//
// Spawning one puff per frame instead made a trail's density a function of how
// fast the machine rendered: at 2950 fps a single rocket produced over a
// thousand live particles and two of them filled the whole pool, silently
// dropping every other effect. `max_burst` bounds the catch-up after a hitch.
void particles_emit_rate(ParticleSystem& ps, Rng& rng, Vec3 pos, float* accum, float dt,
                         float per_second, int max_burst);
// A tracer that actually travels: a short streak of dots flying from the muzzle
// and terminating at the impact point, rather than the whole line appearing at
// once. `dots` is the streak length in particles - keep it small, a shotgun
// fires seven of these at once.
void particles_tracer(ParticleSystem& ps, Vec3 start, Vec3 end, Vec3 color, float speed,
                      int dots, float size);
// Dust and sparks where a round meets geometry, delayed to coincide with the
// tracer's arrival. `incoming` is the direction of travel; debris sprays back.
void particles_impact(ParticleSystem& ps, Rng& rng, Vec3 pos, Vec3 incoming, float delay);

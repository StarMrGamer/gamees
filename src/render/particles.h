#pragma once

#include "core/rng.h"
#include "render/renderer.h"

constexpr int MAX_PARTICLES = 2048;

struct Particle {
  Vec3 pos, vel;
  Vec3 color;
  float life, max_life, size;
  bool gravity;
};

struct ParticleSystem {
  Particle pool[MAX_PARTICLES];
  int alive;
  Mesh dynamic;
};

void particles_init(ParticleSystem& ps);
void particles_update(ParticleSystem& ps, float dt);
void particles_render(ParticleSystem& ps, Renderer& r, const Camera& cam);
void particles_explosion(ParticleSystem& ps, Rng& rng, Vec3 pos);
void particles_sparks(ParticleSystem& ps, Rng& rng, Vec3 pos, Vec3 normal);
void particles_muzzle_flash(ParticleSystem& ps, Rng& rng, Vec3 pos, Vec3 dir);
void particles_trail(ParticleSystem& ps, Rng& rng, Vec3 pos);
void particles_tracer(ParticleSystem& ps, Vec3 start, Vec3 end, Vec3 color);

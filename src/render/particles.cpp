#include "render/particles.h"

#include <cstring>

static void spawn_particle(ParticleSystem& ps, Particle p) {
  // Bursts spawn dozens at once; scanning from the last slot instead of zero
  // keeps this near O(1) instead of repeatedly walking the whole pool.
  for (int n = 0; n < MAX_PARTICLES; ++n) {
    int i = ps.spawn_cursor + n;
    if (i >= MAX_PARTICLES) i -= MAX_PARTICLES;
    if (ps.pool[i].life <= 0.0f) {
      ps.pool[i] = p;
      ps.spawn_cursor = i + 1 < MAX_PARTICLES ? i + 1 : 0;
      return;
    }
  }
}

void particles_init(ParticleSystem& ps) {
  std::memset(&ps, 0, sizeof(ps));
}

void particles_update(ParticleSystem& ps, float dt) {
  ps.alive = 0;
  for (int i = 0; i < MAX_PARTICLES; ++i) {
    Particle& p = ps.pool[i];
    if (p.life <= 0.0f) continue;
    p.life -= dt;
    if (p.life <= 0.0f) continue;
    if (p.gravity) p.vel.y -= 12.0f * dt;
    p.pos += p.vel * dt;
    ++ps.alive;
  }
}

void particles_render(ParticleSystem& ps, Renderer& r, const Camera&) {
  // Queue into the renderer's dynamic batch; the caller flushes them together
  // with players/rockets/pickups in a single draw call.
  for (int i = 0; i < MAX_PARTICLES; ++i) {
    const Particle& p = ps.pool[i];
    if (p.life <= 0.0f) continue;
    float a = p.life / p.max_life;
    renderer_queue_box(r, p.pos, {p.size, p.size, p.size}, p.color * a, 0.0f);
  }
}

void particles_explosion(ParticleSystem& ps, Rng& rng, Vec3 pos) {
  for (int i = 0; i < 48; ++i) {
    Vec3 v = vec3_normalize({rng_float(rng, -1, 1), rng_float(rng, -0.2f, 1), rng_float(rng, -1, 1)});
    spawn_particle(ps, {pos, v * rng_float(rng, 2, 12), {1.0f, 0.48f, 0.10f}, 0.55f, 0.55f, rng_float(rng, 0.06f, 0.16f), true});
  }
}

void particles_sparks(ParticleSystem& ps, Rng& rng, Vec3 pos, Vec3 normal) {
  for (int i = 0; i < 16; ++i) {
    Vec3 v = vec3_normalize(normal + Vec3{rng_float(rng, -0.6f, 0.6f), rng_float(rng, -0.6f, 0.6f), rng_float(rng, -0.6f, 0.6f)});
    spawn_particle(ps, {pos, v * rng_float(rng, 4, 10), {1.0f, 0.9f, 0.35f}, 0.25f, 0.25f, 0.04f, true});
  }
}

void particles_muzzle_flash(ParticleSystem& ps, Rng&, Vec3 pos, Vec3 dir) {
  spawn_particle(ps, {pos + dir * 0.4f, dir * 1.0f, {1.0f, 0.75f, 0.25f}, 0.08f, 0.08f, 0.25f, false});
}

void particles_trail(ParticleSystem& ps, Rng& rng, Vec3 pos) {
  spawn_particle(ps, {pos, {rng_float(rng, -0.2f, 0.2f), rng_float(rng, -0.2f, 0.2f), rng_float(rng, -0.2f, 0.2f)}, {0.42f, 0.42f, 0.42f}, 0.45f, 0.45f, 0.08f, false});
}

// A bullet tracer: a short-lived line of bright dots from muzzle to impact.
void particles_tracer(ParticleSystem& ps, Vec3 start, Vec3 end, Vec3 color) {
  Vec3 delta = end - start;
  float len = vec3_length(delta);
  if (len < 0.05f) return;
  Vec3 dir = delta / len;
  int count = static_cast<int>(len / 0.7f) + 2;
  if (count > 36) count = 36;
  for (int i = 0; i < count; ++i) {
    float f = static_cast<float>(i) / static_cast<float>(count - 1);
    // Fade and thin toward the impact end so it reads as a streak, not a rod.
    float taper = 1.0f - 0.5f * f;
    spawn_particle(ps, {start + dir * (len * f), {0.0f, 0.0f, 0.0f}, color * taper,
                        0.06f, 0.06f, 0.045f, false});
  }
}

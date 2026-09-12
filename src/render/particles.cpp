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
    if (p.delay > 0.0f) {
      p.delay -= dt;
      ++ps.alive;
      continue;  // not born yet: it must not age, move or draw
    }
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
    if (p.life <= 0.0f || p.delay > 0.0f) continue;
    float a = p.life / p.max_life;
    renderer_queue_box(r, p.pos, {p.size, p.size, p.size}, p.color * a, 0.0f);
  }
}

void particles_explosion(ParticleSystem& ps, Rng& rng, Vec3 pos) {
  for (int i = 0; i < 48; ++i) {
    Vec3 v = vec3_normalize({rng_float(rng, -1, 1), rng_float(rng, -0.2f, 1), rng_float(rng, -1, 1)});
    spawn_particle(ps, {pos, v * rng_float(rng, 2, 12), {1.0f, 0.48f, 0.10f}, 0.55f, 0.55f, rng_float(rng, 0.06f, 0.16f), true, 0.0f});
  }
}

void particles_sparks(ParticleSystem& ps, Rng& rng, Vec3 pos, Vec3 normal) {
  for (int i = 0; i < 16; ++i) {
    Vec3 v = vec3_normalize(normal + Vec3{rng_float(rng, -0.6f, 0.6f), rng_float(rng, -0.6f, 0.6f), rng_float(rng, -0.6f, 0.6f)});
    spawn_particle(ps, {pos, v * rng_float(rng, 4, 10), {1.0f, 0.9f, 0.35f}, 0.25f, 0.25f, 0.04f, true, 0.0f});
  }
}

void particles_muzzle_flash(ParticleSystem& ps, Rng&, Vec3 pos, Vec3 dir) {
  spawn_particle(ps, {pos + dir * 0.4f, dir * 1.0f, {1.0f, 0.75f, 0.25f}, 0.08f, 0.08f, 0.25f, false, 0.0f});
}

void particles_trail(ParticleSystem& ps, Rng& rng, Vec3 pos) {
  spawn_particle(ps, {pos, {rng_float(rng, -0.2f, 0.2f), rng_float(rng, -0.2f, 0.2f), rng_float(rng, -0.2f, 0.2f)}, {0.42f, 0.42f, 0.42f}, 0.45f, 0.45f, 0.08f, false, 0.0f});
}

void particles_emit_rate(ParticleSystem& ps, Rng& rng, Vec3 pos, float* accum, float dt,
                         float per_second, int max_burst) {
  if (!accum || per_second <= 0.0f || dt <= 0.0f) return;
  const float interval = 1.0f / per_second;
  *accum += dt;
  int budget = max_burst > 0 ? max_burst : 1;
  while (*accum >= interval && budget-- > 0) {
    *accum -= interval;
    particles_trail(ps, rng, pos);
  }
  // A long frame must not leave a debt that fires as a burst next frame.
  if (*accum > interval) *accum = 0.0f;
}

// A tracer that travels.
//
// The old one spawned the entire line at once - up to 36 dots hanging in the
// air for 60 ms. That reads as a laser rather than a round in flight, and it
// was ruinous for the pool: a shotgun blast traced seven pellets at 36 dots
// each, 252 of the 2048 particles from a single shot, starving explosions and
// sparks of slots.
//
// Instead a short streak is launched at the muzzle with real velocity. Each dot
// starts further back and is given exactly enough life to cover its own head
// start, so the whole streak arrives together and vanishes into the surface
// rather than sailing through it.
void particles_tracer(ParticleSystem& ps, Vec3 start, Vec3 end, Vec3 color, float speed,
                      int dots, float size) {
  Vec3 delta = end - start;
  float len = vec3_length(delta);
  if (len < 0.05f) return;
  Vec3 dir = delta / len;
  if (speed < 1.0f) speed = 1.0f;
  if (dots < 1) dots = 1;
  // A longer shot gets a longer dash, so distance reads as distance instead of
  // the same stub taking longer to arrive.
  float spacing = clampf(len * 0.03f, 0.16f, 0.8f);
  for (int i = 0; i < dots; ++i) {
    float back = spacing * static_cast<float>(i);
    float life = (len + back) / speed;
    float t = dots > 1 ? static_cast<float>(i) / static_cast<float>(dots - 1) : 0.0f;
    float head = 1.0f - 0.55f * t;  // brightest at the leading dot
    spawn_particle(ps, {start - dir * back, dir * speed, color * head, life, life,
                        size * (0.55f + 0.45f * head), false, 0.0f});
  }
}

void particles_impact(ParticleSystem& ps, Rng& rng, Vec3 pos, Vec3 incoming, float delay) {
  // Debris sprays back along the incoming direction, spread into a cone.
  Vec3 back = -incoming;
  for (int i = 0; i < 5; ++i) {
    Vec3 v = vec3_normalize(back + Vec3{rng_float(rng, -0.75f, 0.75f),
                                        rng_float(rng, -0.4f, 0.85f),
                                        rng_float(rng, -0.75f, 0.75f)});
    spawn_particle(ps, {pos, v * rng_float(rng, 1.6f, 5.0f), {0.78f, 0.72f, 0.60f},
                        0.30f, 0.30f, rng_float(rng, 0.025f, 0.055f), true, delay});
  }
  // One brighter flash right at the surface.
  spawn_particle(ps, {pos, back * 0.6f, {1.0f, 0.85f, 0.5f}, 0.08f, 0.08f, 0.10f, false, delay});
}

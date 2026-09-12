#include "client/gunfeel.h"

#include "game/tuning.h"

#include <cmath>

namespace {

// How hard each weapon kicks the view, in radians. Scaled roughly by damage
// per shot rather than by fire rate: the LMG is fast but light, the rocket is
// slow and heavy.
struct PunchSpec {
  float pitch;
  float yaw;
};

PunchSpec punch_for(uint8_t weapon) {
  switch (weapon) {
    case WEAPON_SHOTGUN: return {0.052f, 0.012f};
    case WEAPON_ROCKET: return {0.060f, 0.008f};
    case WEAPON_SNIPER: return {0.085f, 0.004f};  // heaviest kick, almost no sway
    // The rifle and the LMG kick nothing. They are the sustained-fire weapons,
    // and a view that moves on every shot fights the player's own tracking
    // instead of rewarding it. They still get the viewmodel recoil and the
    // muzzle flash, so the shot is just as visible - it simply does not shove
    // the camera.
    case WEAPON_LMG: return {0.0f, 0.0f};
    default: return {0.0f, 0.0f};  // rifle
  }
}

// Spring constants for the punch. Stiff enough to snap back before the next
// shot at the rifle's 0.12 s cadence, damped enough not to overshoot into a
// visible bounce.
constexpr float PUNCH_STIFFNESS = 190.0f;
constexpr float PUNCH_DAMPING = 22.0f;

// Longest step the integrator stays stable at. A spring this stiff diverges
// above roughly 1/70 s, so a slow frame - or a hitch - has to be walked in
// pieces rather than taken whole. Clamping the frame time alone is not enough:
// a single 0.1 s step still explodes.
constexpr float PUNCH_MAX_STEP = 1.0f / 240.0f;

void spring(float* value, float* vel, float dt) {
  while (dt > 0.0f) {
    float step = dt > PUNCH_MAX_STEP ? PUNCH_MAX_STEP : dt;
    // Semi-implicit Euler: damped and stable at this step size.
    *vel += (-PUNCH_STIFFNESS * (*value) - PUNCH_DAMPING * (*vel)) * step;
    *value += *vel * step;
    dt -= step;
  }
}

float approach(float current, float target, float rate, float dt) {
  float d = target - current;
  float step = rate * dt;
  if (d > step) return current + step;
  if (d < -step) return current - step;
  return target;
}

}  // namespace

float gunfeel_weapon_interval(uint8_t weapon) {
  switch (weapon) {
    case WEAPON_ROCKET: return ROCKET_INTERVAL;
    case WEAPON_SHOTGUN: return SHOTGUN_INTERVAL;
    case WEAPON_LMG: return LMG_INTERVAL;
    case WEAPON_SNIPER: return SNIPER_INTERVAL;
    default: return RIFLE_INTERVAL;
  }
}

uint8_t gunfeel_weapon_sound(uint8_t weapon) {
  switch (weapon) {
    case WEAPON_ROCKET: return SND_ROCKET_LAUNCH;
    case WEAPON_SHOTGUN: return SND_SHOTGUN;
    case WEAPON_LMG: return SND_LMG;
    case WEAPON_SNIPER: return SND_SNIPER;
    default: return SND_RIFLE;
  }
}

void gunfeel_reset(GunFeel& g) {
  g = GunFeel{};
}

void gunfeel_update(GunFeel& g, float dt, float aim_yaw, float aim_pitch, Vec3 velocity,
                    bool on_ground) {
  if (dt <= 0.0f) return;
  if (dt > 0.1f) dt = 0.1f;  // a hitch must not fling the spring

  if (g.cooldown > 0.0f) g.cooldown -= dt;
  spring(&g.punch_pitch, &g.punch_vel_pitch, dt);
  spring(&g.punch_yaw, &g.punch_vel_yaw, dt);
  g.kick = approach(g.kick, 0.0f, 6.0f, dt);

  // The viewmodel trails a fast turn and catches up, which is most of what
  // sells the gun as an object being carried rather than painted on the lens.
  // The previous aim lives in the struct, not in a static: two GunFeel
  // instances (a split screen, a test) would otherwise share one turn history.
  if (!g.aim_seen) {
    g.last_yaw = aim_yaw;
    g.last_pitch = aim_pitch;
    g.aim_seen = true;
  }
  float dyaw = angle_wrap(aim_yaw - g.last_yaw);
  float dpitch = aim_pitch - g.last_pitch;
  g.last_yaw = aim_yaw;
  g.last_pitch = aim_pitch;
  g.sway_yaw = clampf(g.sway_yaw - dyaw * 0.35f, -0.09f, 0.09f);
  g.sway_pitch = clampf(g.sway_pitch - dpitch * 0.35f, -0.09f, 0.09f);
  g.sway_yaw = approach(g.sway_yaw, 0.0f, 0.55f, dt);
  g.sway_pitch = approach(g.sway_pitch, 0.0f, 0.55f, dt);

  float speed = std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z);
  float want_bob = on_ground ? clampf(speed / GROUND_MAX_SPEED, 0.0f, 1.4f) : 0.0f;
  g.bob_amount = approach(g.bob_amount, want_bob, 3.0f, dt);
  g.bob_phase += dt * (6.0f + speed * 0.45f);
  if (g.bob_phase > PI * 4.0f) g.bob_phase -= PI * 4.0f;
}

bool gunfeel_try_fire(GunFeel& g, uint8_t weapon, bool want_fire, bool alive) {
  if (!alive || !want_fire || g.cooldown > 0.0f) return false;
  g.cooldown = gunfeel_weapon_interval(weapon);
  PunchSpec spec = punch_for(weapon);
  // Kick upward, and alternate the horizontal component so a held burst walks
  // rather than climbing in a straight line.
  g.punch_vel_pitch -= spec.pitch * PUNCH_STIFFNESS * 0.22f;
  float side = (g.shots_fired & 1u) ? 1.0f : -1.0f;
  g.punch_vel_yaw += side * spec.yaw * PUNCH_STIFFNESS * 0.22f;
  g.kick = 1.0f;
  ++g.shots_fired;
  return true;
}

void gunfeel_update_zoom(GunFeel& g, bool want_zoom, bool can_zoom, float dt) {
  float target = (want_zoom && can_zoom) ? 1.0f : 0.0f;
  float rate = SNIPER_ZOOM_TIME > 0.0f ? dt / SNIPER_ZOOM_TIME : 1.0f;
  g.zoom = approach(g.zoom, target, 1.0f / (SNIPER_ZOOM_TIME > 0.0f ? SNIPER_ZOOM_TIME : 1.0f),
                    dt);
  g.zoom = clampf(g.zoom, 0.0f, 1.0f);
  (void)rate;
}

float gunfeel_fov_degrees(const GunFeel& g, float base_fov) {
  return lerp(base_fov, SNIPER_ZOOM_FOV, clampf(g.zoom, 0.0f, 1.0f));
}

float gunfeel_sensitivity_scale(const GunFeel& g) {
  // Matching the sensitivity to the zoom keeps the same mouse travel covering
  // the same distance on screen. Without it a scoped view is unusable.
  return lerp(1.0f, SNIPER_ZOOM_SENSITIVITY, clampf(g.zoom, 0.0f, 1.0f));
}

void gunfeel_view_punch(const GunFeel& g, float* yaw_out, float* pitch_out) {
  if (yaw_out) *yaw_out = g.punch_yaw;
  if (pitch_out) *pitch_out = g.punch_pitch;
}

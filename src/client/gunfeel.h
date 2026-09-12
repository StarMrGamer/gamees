#pragma once

#include "core/math.h"
#include "game/game_state.h"

// Client-side weapon feel: the half of shooting that must not wait for the
// server to agree.
//
// Every weapon effect used to be driven by the server's event stream, so
// pressing fire produced nothing at all until a snapshot came back - a tick of
// server latency plus the round trip, before a single pixel or sample moved.
// That is what "the gun feels unresponsive" was; no amount of animation fixes
// it, because the animation was also waiting on the packet.
//
// So the client mirrors the server's fire rule and plays its own shot
// immediately. The server stays authoritative for damage - this only decides
// when the local player sees and hears their own gun.
struct GunFeel {
  float cooldown;         // local mirror of Player::fire_cooldown
  // View punch, as a spring: kicked on fire, pulled back to centre. Visual
  // only - the aim sent to the server is untouched, so this changes how the
  // gun feels without changing where bullets go or what the bot has to beat.
  float punch_pitch, punch_yaw;
  float punch_vel_pitch, punch_vel_yaw;
  float kick;             // viewmodel recoil, 1 at the shot, decaying to 0
  float sway_yaw, sway_pitch;  // viewmodel lagging behind a fast turn
  float bob_phase;        // walk cycle, drives the idle/movement bob
  float bob_amount;
  float last_yaw, last_pitch;  // previous frame's aim, for the sway
  bool aim_seen;
  uint32_t shots_fired;   // local count, for debugging and tests
  float zoom;             // 0 = hip, 1 = fully scoped
};

void gunfeel_reset(GunFeel& g);

// Seconds between shots for a weapon, matching the server exactly. If these
// ever disagree the local gun fires at a different rate from the real one.
float gunfeel_weapon_interval(uint8_t weapon);

// Advances timers. `aim_yaw`/`aim_pitch` are where the player is looking, used
// to make the viewmodel lag behind fast turns.
void gunfeel_update(GunFeel& g, float dt, float aim_yaw, float aim_pitch, Vec3 velocity,
                    bool on_ground);

// True when the local player's shot goes off this frame, which is when the
// caller should play the muzzle flash, tracer and sound. Mirrors the server's
// rule: alive, holding fire, and off cooldown.
bool gunfeel_try_fire(GunFeel& g, uint8_t weapon, bool want_fire, bool alive);

// Advances the scope toward `want_zoom` (0 or 1). Zoom is a client-only view
// change: the simulation never sees it, so it costs nothing on the wire and
// cannot desync.
void gunfeel_update_zoom(GunFeel& g, bool want_zoom, bool can_zoom, float dt);
// Vertical field of view in degrees, and the matching aim sensitivity scale.
float gunfeel_fov_degrees(const GunFeel& g, float base_fov);
float gunfeel_sensitivity_scale(const GunFeel& g);

// View punch to add to the camera angles. Not added to the input.
void gunfeel_view_punch(const GunFeel& g, float* yaw_out, float* pitch_out);

// The sound the server will emit for this weapon. Mirrored so the local shot
// plays the same report the remote one would; if these drift, your own gun
// sounds different from everyone else's.
uint8_t gunfeel_weapon_sound(uint8_t weapon);

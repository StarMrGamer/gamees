#include "client/game_client.h"

#include "audio/mixer.h"
#include "audio/synth.h"
#include "client/client.h"
#include "client/config.h"
#include "core/log.h"
#include "game/collision.h"
#include "game/map.h"
#include "game/tuning.h"
#include "game/weapons.h"
#include "render/hud.h"
#include "render/particles.h"
#include "render/renderer.h"

#include <SDL3/SDL.h>

#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <thread>

struct KillFeedItem {
  char text[64];
  float time_left;
};

constexpr float SOURCE_MOUSE_DEGREES_PER_COUNT = 0.022f;
constexpr float MIN_SENSITIVITY = 0.1f;
constexpr float MAX_SENSITIVITY = 20.0f;
constexpr float SENSITIVITY_STEP = 0.1f;
constexpr int SETTINGS_ITEM_COUNT = 7;

// Nanosecond resolution on purpose. SDL_GetTicks() is whole milliseconds, so
// above a few hundred fps the frame delta quantises to 1 ms or 0 ms - which
// makes frametime graphs meaningless and jitters anything scaled by dt.
static double app_seconds() {
  return static_cast<double>(SDL_GetTicksNS()) / 1e9;
}

// Waits until `target` using a coarse sleep for the bulk of the remaining time
// and a short spin for the tail. Sleeping the whole way overshoots by however
// much the scheduler feels like; spinning the whole way burns a core. The
// hand-off point is one millisecond, which is about the worst case for
// SDL_DelayNS granularity on a normal desktop kernel.
static void wait_until(double target) {
  for (;;) {
    double now = app_seconds();
    double remaining = target - now;
    if (remaining <= 0.0) return;
    if (remaining > 0.0015) {
      SDL_DelayNS(static_cast<Uint64>((remaining - 0.001) * 1e9));
    } else {
      SDL_CPUPauseInstruction();
    }
  }
}

static const char* max_fps_label(int max_fps, char* buf, size_t cap) {
  if (max_fps <= 0) return "UNLIMITED";
  std::snprintf(buf, cap, "%d", max_fps);
  return buf;
}

// Cycles through the caps offered in the settings menu.
static int cycle_max_fps(int current, int dir) {
  static const int steps[] = {MAX_FPS_UNLIMITED, 60, 120, 144, 165, 240, 360, 500, 1000};
  const int count = static_cast<int>(sizeof(steps) / sizeof(steps[0]));
  int index = 0;
  for (int i = 0; i < count; ++i) {
    if (steps[i] == current) { index = i; break; }
  }
  index = (index + dir % count + count) % count;
  return steps[index];
}

static float sanitize_sensitivity(float sensitivity) {
  if (!std::isfinite(sensitivity)) return 3.0f;
  return clampf(sensitivity, MIN_SENSITIVITY, MAX_SENSITIVITY);
}

static float mouse_radians_per_count(float sensitivity) {
  return sensitivity * SOURCE_MOUSE_DEGREES_PER_COUNT * PI / 180.0f;
}

static uint8_t cycle_jump_bind(uint8_t bind, int dir) {
  int v = static_cast<int>(bind);
  v = (v + dir + 3) % 3;
  return static_cast<uint8_t>(v);
}

static bool jump_input_active(const ClientSettings& settings, const bool* keys, float wheel_y) {
  if (settings.jump_bind == JUMP_BIND_MWHEEL_UP) return wheel_y > 0.0f;
  if (settings.jump_bind == JUMP_BIND_MWHEEL_DOWN) return wheel_y < 0.0f;
  return keys[SDL_SCANCODE_SPACE];
}

static uint8_t cycle_airjump_bind(uint8_t bind, int dir) {
  int v = static_cast<int>(bind);
  v = (v + dir + 4) % 4;
  return static_cast<uint8_t>(v);
}

static bool airjump_input_active(const ClientSettings& settings, const bool* keys, float wheel_y) {
  return airjump_bind_pressed(settings.airjump_bind, keys[SDL_SCANCODE_LALT],
                              keys[SDL_SCANCODE_SPACE], wheel_y);
}

static void set_mouse_capture(SDL_Window* window, bool capture) {
  SDL_SetWindowRelativeMouseMode(window, capture);
  SDL_SetWindowMouseGrab(window, capture);
  SDL_SetWindowKeyboardGrab(window, capture);
}

static Vec3 player_color(int index) {
  static const Vec3 colors[MAX_PLAYERS] = {
    {0.90f, 0.20f, 0.20f}, {0.20f, 0.55f, 1.00f}, {0.15f, 0.90f, 0.35f}, {1.00f, 0.78f, 0.18f},
    {0.85f, 0.30f, 1.00f}, {0.10f, 0.88f, 0.82f}, {1.00f, 0.48f, 0.20f}, {0.85f, 0.85f, 0.90f},
  };
  return colors[index % MAX_PLAYERS];
}

static const char* weapon_name(uint8_t weapon) {
  switch (weapon) {
    case WEAPON_ROCKET: return "ROCKET";
    case WEAPON_SHOTGUN: return "SHOTGUN";
    case WEAPON_LMG: return "LMG";
    default: return "RIFLE";
  }
}

static Vec3 player_class_tint(uint8_t player_class) {
  switch (player_class) {
    case CLASS_SCOUT: return {0.72f, 1.10f, 1.18f};
    case CLASS_TANK: return {1.18f, 0.90f, 0.72f};
    default: return {1.0f, 1.0f, 1.0f};
  }
}

static const char* player_label(const GameState& s, int index) {
  if (index < 0 || index >= MAX_PLAYERS) return "world";
  const Player& p = s.players[index];
  return p.name[0] ? p.name : "player";
}

static void hud_text_shadow(Hud& hud, float x, float y, float scale, Vec3 color, const char* fmt, ...) {
  char text[256];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(text, sizeof(text), fmt, args);
  va_end(args);
  hud_text(hud, x + 2.0f, y + 2.0f, scale, {0.02f, 0.025f, 0.03f}, "%s", text);
  hud_text(hud, x, y, scale, color, "%s", text);
}

static void hud_center_text(Hud& hud, int w, int h, float y, float scale, Vec3 color, const char* text) {
  float x = (static_cast<float>(w) - hud_text_width(text, scale)) * 0.5f;
  hud_text_shadow(hud, x, y > 0.0f ? y : static_cast<float>(h) * 0.5f, scale, color, "%s", text);
}

static int sorted_players(const GameState& s, int out[MAX_PLAYERS]) {
  int count = 0;
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    if (s.players[i].active) out[count++] = i;
  }
  for (int i = 1; i < count; ++i) {
    int v = out[i];
    int j = i - 1;
    while (j >= 0) {
      const Player& left = s.players[out[j]];
      const Player& right = s.players[v];
      bool right_before_left = right.frags > left.frags ||
                               (right.frags == left.frags && v < out[j]);
      if (!right_before_left) break;
      out[j + 1] = out[j];
      --j;
    }
    out[j + 1] = v;
  }
  return count;
}

static void draw_scoreboard(Hud& hud, int w, int h, const GameState& s, int local_index) {
  int order[MAX_PLAYERS]{};
  int count = sorted_players(s, order);
  float panel_w = 520.0f;
  float panel_h = 92.0f + static_cast<float>(count) * 30.0f;
  float x = (static_cast<float>(w) - panel_w) * 0.5f;
  float y = (static_cast<float>(h) - panel_h) * 0.5f;
  hud_rect(hud, x, y, panel_w, panel_h, {0.03f, 0.035f, 0.04f}, 0.78f);
  hud_text_shadow(hud, x + 24.0f, y + 22.0f, 1.6f, {0.90f, 0.95f, 1.0f}, "SCOREBOARD");
  hud_text_shadow(hud, x + 24.0f, y + 56.0f, 1.0f, {0.62f, 0.72f, 0.82f}, "PLAYER");
  hud_text_shadow(hud, x + 230.0f, y + 56.0f, 1.0f, {0.62f, 0.72f, 0.82f}, "CLASS");
  hud_text_shadow(hud, x + 360.0f, y + 56.0f, 1.0f, {0.62f, 0.72f, 0.82f}, "FRAGS");
  hud_text_shadow(hud, x + 448.0f, y + 56.0f, 1.0f, {0.62f, 0.72f, 0.82f}, "HP");
  for (int row = 0; row < count; ++row) {
    int i = order[row];
    const Player& p = s.players[i];
    float row_y = y + 86.0f + static_cast<float>(row) * 30.0f;
    Vec3 color = i == local_index ? Vec3{1.0f, 0.86f, 0.35f} : Vec3{0.88f, 0.92f, 0.96f};
    if (!p.alive) color = color * 0.65f;
    hud_text_shadow(hud, x + 24.0f, row_y, 1.15f, color, "%s", player_label(s, i));
    hud_text_shadow(hud, x + 230.0f, row_y, 1.05f, color, "%s", player_class_name(p.player_class));
    hud_text_shadow(hud, x + 374.0f, row_y, 1.15f, color, "%d", p.frags);
    hud_text_shadow(hud, x + 452.0f, row_y, 1.15f, color, p.alive ? "%.0f" : "OUT", p.health);
  }
}

static void draw_settings_menu(Hud& hud, int w, int h, const ClientSettings& settings, int selected) {
  float panel_w = 460.0f;
  float panel_h = 360.0f;
  float x = (static_cast<float>(w) - panel_w) * 0.5f;
  float y = (static_cast<float>(h) - panel_h) * 0.5f;
  hud_rect(hud, x, y, panel_w, panel_h, {0.03f, 0.035f, 0.04f}, 0.88f);
  hud_text_shadow(hud, x + 28.0f, y + 24.0f, 1.55f, {0.90f, 0.95f, 1.0f}, "SETTINGS");

  Vec3 active{1.0f, 0.86f, 0.35f};
  Vec3 idle{0.86f, 0.90f, 0.94f};
  auto prefix = [selected](int item) { return selected == item ? "> " : "  "; };
  auto color = [&](int item) { return selected == item ? active : idle; };
  hud_text_shadow(hud, x + 36.0f, y + 74.0f, 1.20f, color(0), "%sRESUME", prefix(0));
  hud_text_shadow(hud, x + 36.0f, y + 112.0f, 1.20f, color(1),
                  "%sSENSITIVITY %.2f", prefix(1), settings.sensitivity);
  hud_text_shadow(hud, x + 36.0f, y + 150.0f, 1.20f, color(2),
                  "%sJUMP %s", prefix(2), client_jump_bind_name(settings.jump_bind));
  hud_text_shadow(hud, x + 36.0f, y + 188.0f, 1.20f, color(3),
                  "%sDOUBLE JUMP %s", prefix(3), client_airjump_bind_name(settings.airjump_bind));
  char fps_buf[16];
  hud_text_shadow(hud, x + 36.0f, y + 226.0f, 1.20f, color(4),
                  "%sMAX FPS %s", prefix(4), max_fps_label(settings.max_fps, fps_buf,
                                                           sizeof(fps_buf)));
  hud_text_shadow(hud, x + 36.0f, y + 264.0f, 1.20f, color(5),
                  "%sVSYNC %s", prefix(5), settings.vsync ? "ON" : "OFF");
  hud_text_shadow(hud, x + 36.0f, y + 302.0f, 1.20f, color(6), "%sQUIT", prefix(6));
}

static void add_kill_feed(KillFeedItem feed[4], const GameState& s, const GameEvent& e) {
  for (int i = 3; i > 0; --i) feed[i] = feed[i - 1];
  KillFeedItem& item = feed[0];
  item.time_left = 4.0f;
  if (e.a == e.b) {
    std::snprintf(item.text, sizeof(item.text), "%s self-fragged", player_label(s, e.b));
  } else {
    std::snprintf(item.text, sizeof(item.text), "%s fragged %s",
                  player_label(s, e.a), player_label(s, e.b));
  }
}

static Vec3 weapon_tracer_color(uint8_t sound) {
  switch (sound) {
    case SND_SHOTGUN: return {1.0f, 0.62f, 0.24f};
    case SND_LMG: return {1.0f, 0.86f, 0.42f};
    default: return {0.66f, 0.90f, 1.0f};  // rifle
  }
}

// Draw one hitscan tracer from muzzle to its first wall/player intersection,
// with a spark burst on a wall impact. Endpoints are resolved against the
// client's own view + map, so tracers are pure local juice (no protocol cost).
static void trace_one(ParticleSystem& particles, Rng& rng, const GameState& view, const Map& map,
                      int shooter, Vec3 muzzle, Vec3 dir, float range, Vec3 color) {
  float wall_t = ray_map(map, muzzle, dir, range);
  float hit_t = wall_t;
  int hit = find_player_ray_hit(view, muzzle, dir, wall_t, shooter, &hit_t);
  float end_t = hit >= 0 ? hit_t : wall_t;
  Vec3 end = muzzle + dir * end_t;
  particles_tracer(particles, muzzle, end, color);
  if (hit < 0 && wall_t < range) {
    particles_sparks(particles, rng, end, dir * -1.0f);
  }
}

// Muzzle flash + tracers for one shot. The tracer leaves the offset muzzle but
// is aimed with weapon_converged_dir (the exact rule the server shoots with), so
// it lands on the crosshair rather than parallel-offset down-and-right of it.
static void spawn_weapon_fx(ParticleSystem& particles, Rng& rng, const GameState& view,
                            const Map& map, int shooter, uint8_t sound) {
  const Player& sp = view.players[shooter];
  Vec3 aim = vec3_normalize(angles_forward(sp.yaw, sp.pitch));
  Vec3 eye = player_eye_pos(sp);
  Vec3 muzzle = weapon_muzzle_pos(sp);
  particles_muzzle_flash(particles, rng, muzzle, aim);
  if (sound == SND_ROCKET_LAUNCH) return;  // projectile has its own smoke trail

  float range = sound == SND_LMG ? LMG_RANGE : (sound == SND_SHOTGUN ? SHOTGUN_RANGE : RIFLE_RANGE);
  Vec3 base = weapon_converged_dir(view, map, shooter, eye, aim, muzzle, range);
  Vec3 color = weapon_tracer_color(sound);
  if (sound == SND_SHOTGUN) {
    Vec3 right = angles_right(sp.yaw);
    for (int pellet = 0; pellet < SHOTGUN_PELLETS; ++pellet) {
      trace_one(particles, rng, view, map, shooter, muzzle,
                shotgun_pellet_dir(base, right, pellet), SHOTGUN_RANGE, color);
    }
  } else {
    trace_one(particles, rng, view, map, shooter, muzzle, base, range, color);
  }
}

static void handle_events(const ClientEvents& events, const GameState& view, const Map& map,
                          int local_index, Mixer& mixer, ParticleSystem& particles, Rng& fx_rng,
                          KillFeedItem feed[4], float* hitmarker_timer) {
  for (int i = 0; i < events.count; ++i) {
    const GameEvent& e = events.events[i];
    if (e.type == EV_SOUND) {
      audio_play_3d(mixer, e.a, e.pos, 1.0f);
      if (e.a == SND_EXPLOSION) {
        particles_explosion(particles, fx_rng, e.pos);
      } else if (e.a == SND_RIFLE || e.a == SND_LMG || e.a == SND_SHOTGUN ||
                 e.a == SND_ROCKET_LAUNCH) {
        int shooter = e.b;
        if (shooter >= 0 && shooter < MAX_PLAYERS && view.players[shooter].active) {
          spawn_weapon_fx(particles, fx_rng, view, map, shooter, e.a);
        }
      } else if (e.a == SND_PICKUP || e.a == SND_RESPAWN) {
        particles_sparks(particles, fx_rng, e.pos, {0.0f, 1.0f, 0.0f});
      }
    } else if (e.type == EV_HIT) {
      particles_sparks(particles, fx_rng, e.pos, {0.0f, 1.0f, 0.0f});
      if (e.a == local_index && hitmarker_timer) *hitmarker_timer = 0.16f;
    } else if (e.type == EV_KILL) {
      add_kill_feed(feed, view, e);
    }
  }
}

static void draw_status_hud(Hud& hud, int w, int h, const Client& client, const GameState& view,
                            const Map& map, const KillFeedItem feed[4], float hitmarker_timer,
                            float damage_timer, float fps) {
  Vec3 text{0.92f, 0.96f, 0.98f};
  Vec3 warn{1.0f, 0.55f, 0.35f};
  if (damage_timer > 0.0f) {
    float a = clampf(damage_timer / 0.45f, 0.0f, 1.0f) * 0.44f;
    Vec3 red{1.0f, 0.06f, 0.02f};
    float fw = static_cast<float>(w);
    float fh = static_cast<float>(h);
    float edge = (fw < fh ? fw : fh) * 0.10f;
    hud_rect(hud, 0.0f, 0.0f, fw, edge, red, a);
    hud_rect(hud, 0.0f, fh - edge, fw, edge, red, a);
    hud_rect(hud, 0.0f, 0.0f, edge, fh, red, a * 0.72f);
    hud_rect(hud, fw - edge, 0.0f, edge, fh, red, a * 0.72f);
  }

  char fps_text[32];
  std::snprintf(fps_text, sizeof(fps_text), "FPS %.0f", fps);
  hud_text_shadow(hud, static_cast<float>(w) - hud_text_width(fps_text, 1.0f) - 18.0f,
                  18.0f, 1.0f, {0.74f, 0.92f, 0.86f}, "%s", fps_text);
  // Hold P to show your position - handy for reporting map problems.
  const bool* keys = SDL_GetKeyboardState(nullptr);
  if (keys[SDL_SCANCODE_P] && client.player_index >= 0 && client.player_index < MAX_PLAYERS) {
    const Player& lp = view.players[client.player_index];
    char pos_text[64];
    std::snprintf(pos_text, sizeof(pos_text), "X %.1f  Y %.1f  Z %.1f", lp.pos.x, lp.pos.y, lp.pos.z);
    hud_text_shadow(hud, static_cast<float>(w) - hud_text_width(pos_text, 1.0f) - 18.0f,
                    42.0f, 1.0f, {0.95f, 0.92f, 0.66f}, "%s", pos_text);
  }
  for (int i = 0; i < 4; ++i) {
    if (feed[i].time_left > 0.0f) {
      hud_text_shadow(hud, 24.0f, 24.0f + static_cast<float>(i) * 22.0f, 1.0f,
                      {0.96f, 0.90f, 0.74f}, "%s", feed[i].text);
    }
  }

  if (client.state == CLIENT_CONNECTING) {
    hud_center_text(hud, w, h, 96.0f, 1.5f, text, "CONNECTING...");
  } else if (client.state == CLIENT_REJECTED) {
    char msg[128];
    std::snprintf(msg, sizeof(msg), "REJECTED: %s", client.reject_reason);
    hud_center_text(hud, w, h, 96.0f, 1.5f, warn, msg);
  } else if (client.state == CLIENT_DISCONNECTED) {
    hud_center_text(hud, w, h, 96.0f, 1.5f, warn, "DISCONNECTED");
  } else if (client.map_name[0] && std::strcmp(client.map_name, map.name) != 0) {
    hud_center_text(hud, w, h, 96.0f, 1.1f, warn, "SERVER MAP DIFFERS FROM LOCAL MAP");
  }

  if (client.player_index >= 0 && client.player_index < MAX_PLAYERS &&
      view.players[client.player_index].active) {
    const Player& p = view.players[client.player_index];
    Vec3 hp_color = p.health <= 30.0f ? Vec3{1.0f, 0.34f, 0.24f} : Vec3{0.82f, 1.0f, 0.72f};
    hud_text_shadow(hud, 28.0f, static_cast<float>(h) - 112.0f, 1.45f, hp_color, "HP %.0f", p.health);
    hud_text_shadow(hud, 28.0f, static_cast<float>(h) - 78.0f, 1.1f, text, "FRAGS %d / %d", p.frags, view.frag_limit);
    float sx = 30.0f;
    float sy = static_cast<float>(h) - 40.0f;
    for (int i = 0; i < MAX_STAMINA; ++i) {
      Vec3 color = i < p.stamina ? Vec3{0.42f, 0.84f, 1.0f} : Vec3{0.16f, 0.20f, 0.24f};
      hud_rect(hud, sx + static_cast<float>(i) * 34.0f, sy, 25.0f, 8.0f, color, 0.92f);
    }
    float dash_ready = p.dash_cooldown <= 0.0f ? 1.0f : 1.0f - clampf(p.dash_cooldown / DASH_COOLDOWN, 0.0f, 1.0f);
    hud_rect(hud, 30.0f, static_cast<float>(h) - 24.0f, 126.0f, 6.0f, {0.12f, 0.15f, 0.18f}, 0.86f);
    hud_rect(hud, 30.0f, static_cast<float>(h) - 24.0f, 126.0f * dash_ready, 6.0f,
             dash_ready >= 1.0f ? Vec3{0.95f, 0.78f, 0.28f} : Vec3{0.42f, 0.84f, 1.0f}, 0.92f);
    hud_text_shadow(hud, 166.0f, static_cast<float>(h) - 31.0f, 0.82f,
                    dash_ready >= 1.0f ? Vec3{0.95f, 0.78f, 0.28f} : Vec3{0.68f, 0.76f, 0.84f},
                    dash_ready >= 1.0f ? "DASH" : "%.1f", p.dash_cooldown);

    float speed = vec3_length({p.vel.x, 0.0f, p.vel.z});
    char speed_text[48];
    const char* state = p.sliding ? " SLIDE" : (!p.on_ground ? " AIR" : "");
    std::snprintf(speed_text, sizeof(speed_text), "%.0f m/s%s", speed, state);
    Vec3 speed_color = p.sliding ? Vec3{1.0f, 0.78f, 0.28f} : Vec3{0.78f, 0.92f, 1.0f};
    hud_text_shadow(hud, (static_cast<float>(w) - hud_text_width(speed_text, 1.0f)) * 0.5f,
                    static_cast<float>(h) - 58.0f, 1.0f, speed_color, "%s", speed_text);

    const char* weapon = weapon_name(p.weapon);
    float weapon_w = hud_text_width(weapon, 1.35f);
    hud_text_shadow(hud, static_cast<float>(w) - weapon_w - 30.0f,
                    static_cast<float>(h) - 54.0f, 1.35f, text, "%s", weapon);
    const char* cls = player_class_name(p.player_class);
    float cls_w = hud_text_width(cls, 1.0f);
    hud_text_shadow(hud, static_cast<float>(w) - cls_w - 30.0f,
                    static_cast<float>(h) - 82.0f, 1.0f, player_class_tint(p.player_class), "%s", cls);
    if (!p.alive) {
      hud_center_text(hud, w, h, static_cast<float>(h) * 0.62f, 1.4f, warn, "RESPAWNING");
    }
  }

  if (view.match_over) {
    hud_center_text(hud, w, h, 132.0f, 1.4f, {1.0f, 0.86f, 0.35f}, "MATCH OVER");
  }

  if (hitmarker_timer > 0.0f) {
    float cx = static_cast<float>(w) * 0.5f;
    float cy = static_cast<float>(h) * 0.5f;
    hud_rect(hud, cx - 24.0f, cy - 24.0f, 10.0f, 3.0f, {1.0f, 0.25f, 0.20f}, 0.9f);
    hud_rect(hud, cx + 14.0f, cy - 24.0f, 10.0f, 3.0f, {1.0f, 0.25f, 0.20f}, 0.9f);
    hud_rect(hud, cx - 24.0f, cy + 21.0f, 10.0f, 3.0f, {1.0f, 0.25f, 0.20f}, 0.9f);
    hud_rect(hud, cx + 14.0f, cy + 21.0f, 10.0f, 3.0f, {1.0f, 0.25f, 0.20f}, 0.9f);
  }
}

static bool world_to_screen(const Mat4& view_proj, Vec3 pos, int w, int h, float* sx, float* sy) {
  float clip_x = view_proj.m[0] * pos.x + view_proj.m[4] * pos.y +
                 view_proj.m[8] * pos.z + view_proj.m[12];
  float clip_y = view_proj.m[1] * pos.x + view_proj.m[5] * pos.y +
                 view_proj.m[9] * pos.z + view_proj.m[13];
  float clip_z = view_proj.m[2] * pos.x + view_proj.m[6] * pos.y +
                 view_proj.m[10] * pos.z + view_proj.m[14];
  float clip_w = view_proj.m[3] * pos.x + view_proj.m[7] * pos.y +
                 view_proj.m[11] * pos.z + view_proj.m[15];
  if (clip_w <= 0.01f) return false;

  float ndc_x = clip_x / clip_w;
  float ndc_y = clip_y / clip_w;
  float ndc_z = clip_z / clip_w;
  if (ndc_x < -1.0f || ndc_x > 1.0f || ndc_y < -1.0f || ndc_y > 1.0f ||
      ndc_z < -1.0f || ndc_z > 1.0f) {
    return false;
  }

  if (sx) *sx = (ndc_x * 0.5f + 0.5f) * static_cast<float>(w);
  if (sy) *sy = (0.5f - ndc_y * 0.5f) * static_cast<float>(h);
  return true;
}

static void draw_enemy_health_bars(Hud& hud, const Renderer& renderer, const GameState& view,
                                   int local_index, int w, int h) {
  // One projection per frame rather than one per player.
  const Mat4& view_proj = renderer.view_proj;
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    const Player& p = view.players[i];
    if (!p.active || !p.alive || i == local_index) continue;

    float body_h = p.crouching ? PLAYER_CROUCH_HEIGHT : PLAYER_HEIGHT;
    Vec3 anchor = p.pos + Vec3{0.0f, body_h + 0.52f, 0.0f};
    float sx = 0.0f;
    float sy = 0.0f;
    if (!world_to_screen(view_proj, anchor, w, h, &sx, &sy)) continue;

    float dist = vec3_length(p.pos - renderer.camera.pos);
    float bar_w = clampf(84.0f - dist * 1.1f, 42.0f, 84.0f);
    float bar_h = 7.0f;
    float x = sx - bar_w * 0.5f;
    float y = sy - 8.0f;
    float max_health = player_class_max_health(p.player_class);
    float frac = max_health > 0.0f ? clampf(p.health / max_health, 0.0f, 1.0f) : 0.0f;
    Vec3 fill = frac <= 0.30f ? Vec3{1.0f, 0.18f, 0.12f} : Vec3{0.20f, 1.0f, 0.38f};

    hud_rect(hud, x - 2.0f, y - 2.0f, bar_w + 4.0f, bar_h + 4.0f, {0.015f, 0.018f, 0.020f}, 0.84f);
    hud_rect(hud, x, y, bar_w, bar_h, {0.10f, 0.12f, 0.14f}, 0.92f);
    hud_rect(hud, x, y, bar_w * frac, bar_h, fill, 0.96f);
    hud_rect(hud, x, y + bar_h + 2.0f, bar_w, 2.0f, player_class_tint(p.player_class), 0.86f);
  }
}

static PlayerInput sample_input(uint8_t weapon_switch, uint8_t class_switch, const ClientSettings& settings,
                                float* yaw, float* pitch, float dt,
                                float mouse_dx, float mouse_dy, float wheel_y) {
  const bool* keys = SDL_GetKeyboardState(nullptr);
  float fallback_x = 0.0f;
  float fallback_y = 0.0f;
  SDL_MouseButtonFlags mouse_buttons = SDL_GetRelativeMouseState(&fallback_x, &fallback_y);
  if (mouse_dx == 0.0f && mouse_dy == 0.0f) {
    mouse_dx = fallback_x;
    mouse_dy = fallback_y;
  }
  PlayerInput in{};
  if (keys[SDL_SCANCODE_W]) in.buttons |= BTN_FORWARD;
  if (keys[SDL_SCANCODE_S]) in.buttons |= BTN_BACK;
  if (keys[SDL_SCANCODE_A]) in.buttons |= BTN_LEFT;
  if (keys[SDL_SCANCODE_D]) in.buttons |= BTN_RIGHT;
  if (jump_input_active(settings, keys, wheel_y)) in.buttons |= BTN_JUMP;
  if (airjump_input_active(settings, keys, wheel_y)) in.buttons |= BTN_AIRJUMP;
  if (keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_C]) in.buttons |= BTN_CROUCH;
  if (keys[SDL_SCANCODE_LSHIFT]) in.buttons |= BTN_DASH;
  if (keys[SDL_SCANCODE_F] || (mouse_buttons & SDL_BUTTON_LMASK)) in.buttons |= BTN_FIRE;
  if (keys[SDL_SCANCODE_LEFT]) *yaw -= 2.6f * dt;
  if (keys[SDL_SCANCODE_RIGHT]) *yaw += 2.6f * dt;
  if (keys[SDL_SCANCODE_UP]) *pitch += 1.8f * dt;
  if (keys[SDL_SCANCODE_DOWN]) *pitch -= 1.8f * dt;
  float mouse_scale = mouse_radians_per_count(settings.sensitivity);
  *yaw += mouse_dx * mouse_scale;
  *pitch -= mouse_dy * mouse_scale;
  *pitch = clampf(*pitch, -1.35f, 1.35f);
  in.weapon_switch = weapon_switch;
  in.class_switch = class_switch;
  in.yaw = *yaw;
  in.pitch = *pitch;
  return in;
}

static void draw_first_person_hud(Hud& hud, int w, int h, uint8_t weapon) {
  Vec3 white{0.94f, 0.96f, 0.92f};
  float cx = static_cast<float>(w) * 0.5f;
  float cy = static_cast<float>(h) * 0.5f;
  hud_rect(hud, cx - 18.0f, cy - 1.0f, 13.0f, 2.0f, white, 0.92f);
  hud_rect(hud, cx + 5.0f, cy - 1.0f, 13.0f, 2.0f, white, 0.92f);
  hud_rect(hud, cx - 1.0f, cy - 18.0f, 2.0f, 13.0f, white, 0.92f);
  hud_rect(hud, cx - 1.0f, cy + 5.0f, 2.0f, 13.0f, white, 0.92f);

  float gun_x = static_cast<float>(w) - 280.0f;
  float gun_y = static_cast<float>(h) - 150.0f;
  if (weapon == WEAPON_ROCKET) {
    hud_rect(hud, gun_x + 30.0f, gun_y + 52.0f, 190.0f, 42.0f, {0.26f, 0.28f, 0.29f}, 0.96f);
    hud_rect(hud, gun_x + 168.0f, gun_y + 38.0f, 58.0f, 70.0f, {0.74f, 0.28f, 0.10f}, 0.96f);
    hud_rect(hud, gun_x + 55.0f, gun_y + 92.0f, 95.0f, 34.0f, {0.13f, 0.14f, 0.15f}, 0.96f);
  } else {
    hud_rect(hud, gun_x + 84.0f, gun_y + 40.0f, 150.0f, 28.0f, {0.17f, 0.18f, 0.19f}, 0.96f);
    hud_rect(hud, gun_x + 58.0f, gun_y + 64.0f, 130.0f, 58.0f, {0.25f, 0.27f, 0.28f}, 0.96f);
    hud_rect(hud, gun_x + 36.0f, gun_y + 106.0f, 72.0f, 34.0f, {0.10f, 0.11f, 0.12f}, 0.96f);
  }
}

int game_client_main(NetAddress server, const char* player_name, ServerThread* owned_server,
                     const char* map_path, ClientSettings settings) {
  if (owned_server) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  settings.sensitivity = sanitize_sensitivity(settings.sensitivity);

  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
    log_error("SDL init failed: %s", SDL_GetError());
    if (owned_server) server_thread_stop(*owned_server);
    return 1;
  }

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

  SDL_Window* window = SDL_CreateWindow("arena", 1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN);
  if (!window) {
    log_error("window creation failed: %s", SDL_GetError());
    SDL_Quit();
    if (owned_server) server_thread_stop(*owned_server);
    return 1;
  }
  SDL_GLContext gl = SDL_GL_CreateContext(window);
  if (!gl) {
    log_error("GL context creation failed: %s", SDL_GetError());
    SDL_DestroyWindow(window);
    SDL_Quit();
    if (owned_server) server_thread_stop(*owned_server);
    return 1;
  }
  if (!SDL_SetWindowFullscreenMode(window, nullptr)) {
    log_warn("failed to request borderless fullscreen mode: %s", SDL_GetError());
  }
  if (!SDL_SetWindowFullscreen(window, true)) {
    log_warn("failed to enter fullscreen: %s", SDL_GetError());
  }
  SDL_SyncWindow(window);
  set_mouse_capture(window, true);
  SDL_RaiseWindow(window);

  // Set the swap interval explicitly. Leaving it unset means the frame rate
  // depends on whatever the GL driver defaults to, which differs between
  // machines and is not something the player can see or change.
  if (!SDL_GL_SetSwapInterval(settings.vsync ? 1 : 0)) {
    log_warn("failed to set swap interval: %s", SDL_GetError());
  }

  Map map{};
  if (!map_load(map_path ? map_path : "maps/arena.txt", &map)) {
    SDL_GL_DestroyContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    if (owned_server) server_thread_stop(*owned_server);
    return 1;
  }

  Renderer renderer{};
  if (!renderer_init(renderer, map)) {
    SDL_GL_DestroyContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    if (owned_server) server_thread_stop(*owned_server);
    return 1;
  }
  Hud hud{};
  hud_init(hud);
  ParticleSystem particles{};
  particles_init(particles);
  Mixer mixer{};
  audio_init(mixer);
  for (int s = 1; s < SND_COUNT; ++s) audio_register(mixer, s, synth_make(s));

  Client client{};
  if (!client_start(client, server, player_name ? player_name : "player")) {
    log_error("failed to start client socket");
  }

  bool running = true;
  uint8_t selected_weapon = 1;
  uint8_t selected_class = settings.player_class < PLAYER_CLASS_COUNT
    ? static_cast<uint8_t>(settings.player_class + 1)
    : 1;
  bool settings_open = false;
  int settings_selected = 0;
  float yaw = 0.0f;
  float pitch = 0.0f;
  bool aim_initialized = false;
  bool map_mismatch_logged = false;
  float hitmarker_timer = 0.0f;
  float damage_timer = 0.0f;
  float previous_local_health = -1.0f;
  bool previous_local_alive = false;
  KillFeedItem kill_feed[4]{};
  Rng fx_rng{0x9e3779b97f4a7c15ull};
  double fps_accum = 0.0;
  int fps_frames = 0;
  float shown_fps = 0.0f;
  double last = app_seconds();
  double next_frame_time = last;
  while (running) {
    double now = app_seconds();
    float dt = static_cast<float>(now - last);
    last = now;
    fps_accum += dt;
    ++fps_frames;
    if (fps_accum >= 0.25) {
      shown_fps = static_cast<float>(static_cast<double>(fps_frames) / fps_accum);
      fps_accum = 0.0;
      fps_frames = 0;
    }

    SDL_Event ev;
    float mouse_dx = 0.0f;
    float mouse_dy = 0.0f;
    float wheel_y = 0.0f;
    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_EVENT_QUIT || ev.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) running = false;
      if (ev.type == SDL_EVENT_MOUSE_MOTION) {
        mouse_dx += ev.motion.xrel;
        mouse_dy += ev.motion.yrel;
      }
      if (ev.type == SDL_EVENT_MOUSE_WHEEL) {
        float y = ev.wheel.y;
        if (ev.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) y = -y;
        wheel_y += y;
      }
      if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        if (!settings_open) set_mouse_capture(window, true);
      }
      if (ev.type == SDL_EVENT_KEY_DOWN && !ev.key.repeat) {
        if (ev.key.key == SDLK_ESCAPE) {
          settings_open = !settings_open;
          set_mouse_capture(window, !settings_open);
        } else if (settings_open) {
          if (ev.key.key == SDLK_UP) settings_selected = (settings_selected + SETTINGS_ITEM_COUNT - 1) % SETTINGS_ITEM_COUNT;
          if (ev.key.key == SDLK_DOWN) settings_selected = (settings_selected + 1) % SETTINGS_ITEM_COUNT;
          if (settings_selected == 1 && ev.key.key == SDLK_LEFT) {
            settings.sensitivity = sanitize_sensitivity(settings.sensitivity - SENSITIVITY_STEP);
            client_config_save(settings);
          }
          if (settings_selected == 1 && ev.key.key == SDLK_RIGHT) {
            settings.sensitivity = sanitize_sensitivity(settings.sensitivity + SENSITIVITY_STEP);
            client_config_save(settings);
          }
          if (settings_selected == 2 && ev.key.key == SDLK_LEFT) {
            settings.jump_bind = cycle_jump_bind(settings.jump_bind, -1);
            client_config_save(settings);
          }
          if (settings_selected == 2 && ev.key.key == SDLK_RIGHT) {
            settings.jump_bind = cycle_jump_bind(settings.jump_bind, 1);
            client_config_save(settings);
          }
          if (settings_selected == 3 && ev.key.key == SDLK_LEFT) {
            settings.airjump_bind = cycle_airjump_bind(settings.airjump_bind, -1);
            client_config_save(settings);
          }
          if (settings_selected == 3 && ev.key.key == SDLK_RIGHT) {
            settings.airjump_bind = cycle_airjump_bind(settings.airjump_bind, 1);
            client_config_save(settings);
          }
          if (settings_selected == 4 &&
              (ev.key.key == SDLK_LEFT || ev.key.key == SDLK_RIGHT)) {
            settings.max_fps = cycle_max_fps(settings.max_fps,
                                             ev.key.key == SDLK_LEFT ? -1 : 1);
            next_frame_time = app_seconds();
            client_config_save(settings);
          }
          if (settings_selected == 5 &&
              (ev.key.key == SDLK_LEFT || ev.key.key == SDLK_RIGHT)) {
            settings.vsync = !settings.vsync;
            if (!SDL_GL_SetSwapInterval(settings.vsync ? 1 : 0)) {
              log_warn("failed to set swap interval: %s", SDL_GetError());
            }
            client_config_save(settings);
          }
          if (ev.key.key == SDLK_RETURN || ev.key.key == SDLK_KP_ENTER) {
            if (settings_selected == 0) {
              settings_open = false;
              set_mouse_capture(window, true);
            } else if (settings_selected == 6) {
              running = false;
            }
          }
        } else {
          if (ev.key.key == SDLK_1) selected_weapon = 1;
          if (ev.key.key == SDLK_2) selected_weapon = 2;
          if (ev.key.key == SDLK_3) {
            selected_class = 1;
            selected_weapon = 1;
          }
          if (ev.key.key == SDLK_4) {
            selected_class = 2;
            selected_weapon = 1;
          }
          if (ev.key.key == SDLK_5) {
            selected_class = 3;
            selected_weapon = 1;
          }
        }
      }
    }

    ClientEvents events{};
    client_receive(client, now, &events);
    PlayerInput in{};
    if (settings_open) {
      in.weapon_switch = selected_weapon;
      in.class_switch = selected_class;
      in.yaw = yaw;
      in.pitch = pitch;
    } else {
      in = sample_input(selected_weapon, selected_class, settings, &yaw, &pitch, dt, mouse_dx, mouse_dy, wheel_y);
    }
    client_send_input(client, in);

    GameState view;
    client_view_state(client, &view);
    if (client.player_index >= 0 && client.player_index < MAX_PLAYERS &&
        view.players[client.player_index].active) {
      const Player& local = view.players[client.player_index];
      if (previous_local_health >= 0.0f && local.alive &&
          local.health < previous_local_health - 0.25f) {
        damage_timer = 0.35f;
      }
      if (previous_local_alive && !local.alive) {
        damage_timer = 0.55f;
      }
      previous_local_health = local.health;
      previous_local_alive = local.alive;
    } else {
      previous_local_health = -1.0f;
      previous_local_alive = false;
    }
    if (client.state == CLIENT_CONNECTED && client.map_name[0] &&
        std::strcmp(client.map_name, map.name) != 0 && !map_mismatch_logged) {
      log_warn("server map is '%s' but local renderer loaded '%s'", client.map_name, map.name);
      map_mismatch_logged = true;
    }

    Camera cam{};
    if (client.player_index >= 0 && client.player_index < MAX_PLAYERS &&
        view.players[client.player_index].active) {
      const Player& p = view.players[client.player_index];
      if (!aim_initialized) {
        yaw = p.yaw;
        pitch = p.pitch;
        aim_initialized = true;
      }
      float eye_h = p.crouching ? CROUCH_EYE_HEIGHT : EYE_HEIGHT;
      cam.pos = p.pos + Vec3{0.0f, eye_h, 0.0f};
      cam.yaw = yaw;
      cam.pitch = pitch;
      audio_set_listener(mixer, cam.pos, cam.yaw);
    } else {
      aim_initialized = false;
      cam.pos = map.spawns[0] + Vec3{0.0f, EYE_HEIGHT, 0.0f};
      cam.yaw = yaw;
      cam.pitch = pitch;
    }

    handle_events(events, view, map, client.player_index, mixer, particles, fx_rng, kill_feed, &hitmarker_timer);
    particles_update(particles, dt);
    if (hitmarker_timer > 0.0f) hitmarker_timer -= dt;
    if (damage_timer > 0.0f) damage_timer -= dt;
    for (int i = 0; i < 4; ++i) {
      if (kill_feed[i].time_left > 0.0f) kill_feed[i].time_left -= dt;
    }
    int w = 1280;
    int h = 720;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    renderer_begin_frame(renderer, cam, w, h, map);
    renderer_draw_world(renderer);
    // Every transient box — players, rockets, pickups and all particles — goes
    // into one pre-transformed batch and is drawn in a single call.
    renderer_begin_boxes(renderer);
    for (int i = 0; i < MAX_PLAYERS; ++i) {
      const Player& p = view.players[i];
      if (!p.active || !p.alive) continue;
      if (i == client.player_index) continue;
      float body_h = p.crouching ? PLAYER_CROUCH_HEIGHT : PLAYER_HEIGHT;
      Vec3 body_color = player_color(i) * player_class_tint(p.player_class);
      renderer_queue_box(renderer, p.pos + Vec3{0.0f, body_h * 0.5f, 0.0f},
                         {PLAYER_HALF_W * 2.0f, body_h, PLAYER_HALF_W * 2.0f},
                         body_color, p.yaw);
      renderer_queue_box(renderer, p.pos + Vec3{0.0f, body_h + 0.18f, 0.0f},
                         {0.36f, 0.36f, 0.36f}, body_color * 1.15f, p.yaw);
    }
    for (int i = 0; i < MAX_ROCKETS; ++i) {
      const Rocket& r = view.rockets[i];
      if (r.active) {
        particles_trail(particles, fx_rng, r.pos);
        renderer_queue_box(renderer, r.pos, {0.18f, 0.18f, 0.55f}, {1.0f, 0.46f, 0.10f}, 0.0f);
      }
    }
    for (int i = 0; i < view.pickup_count; ++i) {
      const Pickup& p = view.pickups[i];
      if (!p.present) continue;
      Vec3 c = p.pos + Vec3{0.0f, 0.45f, 0.0f};
      renderer_queue_box(renderer, c, {0.70f, 0.18f, 0.18f}, {0.18f, 1.00f, 0.35f}, 0.0f);
      renderer_queue_box(renderer, c, {0.18f, 0.70f, 0.18f}, {0.18f, 1.00f, 0.35f}, 0.0f);
    }
    particles_render(particles, renderer, cam);
    renderer_flush_boxes(renderer);
    uint8_t local_weapon = WEAPON_RIFLE;
    uint8_t local_class = player_class_from_switch(selected_class);
    if (client.player_index >= 0 && client.player_index < MAX_PLAYERS &&
        view.players[client.player_index].active) {
      local_weapon = view.players[client.player_index].weapon;
      local_class = view.players[client.player_index].player_class;
    }
    if (selected_weapon == 1) local_weapon = player_class_primary_weapon(local_class);
    if (selected_weapon == 2) local_weapon = WEAPON_ROCKET;
    renderer_end_frame(renderer);

    hud_begin(hud, w, h);
    draw_enemy_health_bars(hud, renderer, view, client.player_index, w, h);
    draw_first_person_hud(hud, w, h, local_weapon);
    draw_status_hud(hud, w, h, client, view, map, kill_feed, hitmarker_timer, damage_timer, shown_fps);
    const bool* keys = SDL_GetKeyboardState(nullptr);
    if (settings_open) {
      draw_settings_menu(hud, w, h, settings, settings_selected);
    } else if (keys[SDL_SCANCODE_TAB]) {
      draw_scoreboard(hud, w, h, view, client.player_index);
    }
    hud_end(hud);
    SDL_GL_SwapWindow(window);

    // Frame pacing. There is deliberately no sleep in the uncapped case: the
    // old unconditional SDL_Delay(1) here is what pinned the client at roughly
    // 1000 fps regardless of how fast the machine was.
    if (!(SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS)) {
      // Alt-tabbed: no reason to render flat out, and no reason to hold a core.
      SDL_Delay(8);
      next_frame_time = app_seconds();
    } else if (settings.max_fps > 0) {
      double period = 1.0 / static_cast<double>(settings.max_fps);
      next_frame_time += period;
      double now_after = app_seconds();
      // If we fell behind (a hitch, or a cap we cannot hit) start fresh rather
      // than trying to claw back the missed time with a burst of short frames.
      if (next_frame_time < now_after) next_frame_time = now_after;
      else wait_until(next_frame_time);
    }
  }

  client_disconnect(client);
  client_config_save(settings);
  audio_shutdown(mixer);
  SDL_GL_DestroyContext(gl);
  SDL_DestroyWindow(window);
  SDL_Quit();
  if (owned_server) server_thread_stop(*owned_server);
  return 0;
}

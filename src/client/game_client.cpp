#include "client/game_client.h"

#include "audio/mixer.h"
#include "audio/synth.h"
#include "client/client.h"
#include "core/log.h"
#include "game/map.h"
#include "game/tuning.h"
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

static double app_seconds() {
  return static_cast<double>(SDL_GetTicks()) / 1000.0;
}

static Vec3 player_color(int index) {
  static const Vec3 colors[MAX_PLAYERS] = {
    {0.90f, 0.20f, 0.20f}, {0.20f, 0.55f, 1.00f}, {0.15f, 0.90f, 0.35f}, {1.00f, 0.78f, 0.18f},
    {0.85f, 0.30f, 1.00f}, {0.10f, 0.88f, 0.82f}, {1.00f, 0.48f, 0.20f}, {0.85f, 0.85f, 0.90f},
  };
  return colors[index % MAX_PLAYERS];
}

static const char* weapon_name(uint8_t weapon) {
  return weapon == WEAPON_ROCKET ? "ROCKET" : "RIFLE";
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
  hud_text_shadow(hud, x + 360.0f, y + 56.0f, 1.0f, {0.62f, 0.72f, 0.82f}, "FRAGS");
  hud_text_shadow(hud, x + 448.0f, y + 56.0f, 1.0f, {0.62f, 0.72f, 0.82f}, "HP");
  for (int row = 0; row < count; ++row) {
    int i = order[row];
    const Player& p = s.players[i];
    float row_y = y + 86.0f + static_cast<float>(row) * 30.0f;
    Vec3 color = i == local_index ? Vec3{1.0f, 0.86f, 0.35f} : Vec3{0.88f, 0.92f, 0.96f};
    if (!p.alive) color = color * 0.65f;
    hud_text_shadow(hud, x + 24.0f, row_y, 1.15f, color, "%s", player_label(s, i));
    hud_text_shadow(hud, x + 374.0f, row_y, 1.15f, color, "%d", p.frags);
    hud_text_shadow(hud, x + 452.0f, row_y, 1.15f, color, p.alive ? "%.0f" : "OUT", p.health);
  }
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

static void handle_events(const ClientEvents& events, const GameState& view, int local_index,
                          Mixer& mixer, ParticleSystem& particles, Rng& fx_rng,
                          KillFeedItem feed[4], float* hitmarker_timer) {
  for (int i = 0; i < events.count; ++i) {
    const GameEvent& e = events.events[i];
    if (e.type == EV_SOUND) {
      audio_play_3d(mixer, e.a, e.pos, 1.0f);
      if (e.a == SND_EXPLOSION) {
        particles_explosion(particles, fx_rng, e.pos);
      } else if (e.a == SND_RIFLE || e.a == SND_ROCKET_LAUNCH) {
        int shooter = e.b;
        if (shooter >= 0 && shooter < MAX_PLAYERS && view.players[shooter].active) {
          particles_muzzle_flash(particles, fx_rng, e.pos,
                                 angles_forward(view.players[shooter].yaw, view.players[shooter].pitch));
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
                            float fps) {
  Vec3 text{0.92f, 0.96f, 0.98f};
  Vec3 warn{1.0f, 0.55f, 0.35f};
  char fps_text[32];
  std::snprintf(fps_text, sizeof(fps_text), "FPS %.0f", fps);
  hud_text_shadow(hud, static_cast<float>(w) - hud_text_width(fps_text, 1.0f) - 18.0f,
                  18.0f, 1.0f, {0.74f, 0.92f, 0.86f}, "%s", fps_text);
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
    hud_text_shadow(hud, 28.0f, static_cast<float>(h) - 82.0f, 1.45f, hp_color, "HP %.0f", p.health);
    hud_text_shadow(hud, 28.0f, static_cast<float>(h) - 48.0f, 1.1f, text, "FRAGS %d / %d", p.frags, view.frag_limit);
    const char* weapon = weapon_name(p.weapon);
    float weapon_w = hud_text_width(weapon, 1.35f);
    hud_text_shadow(hud, static_cast<float>(w) - weapon_w - 30.0f,
                    static_cast<float>(h) - 54.0f, 1.35f, text, "%s", weapon);
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

static PlayerInput sample_input(uint8_t weapon_switch, float* yaw, float* pitch, float dt,
                                float mouse_dx, float mouse_dy) {
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
  if (keys[SDL_SCANCODE_SPACE]) in.buttons |= BTN_JUMP;
  if (keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_C]) in.buttons |= BTN_CROUCH;
  if (keys[SDL_SCANCODE_LSHIFT]) in.buttons |= BTN_DASH;
  if (keys[SDL_SCANCODE_F] || (mouse_buttons & SDL_BUTTON_LMASK)) in.buttons |= BTN_FIRE;
  if (keys[SDL_SCANCODE_LEFT]) *yaw -= 2.6f * dt;
  if (keys[SDL_SCANCODE_RIGHT]) *yaw += 2.6f * dt;
  if (keys[SDL_SCANCODE_UP]) *pitch += 1.8f * dt;
  if (keys[SDL_SCANCODE_DOWN]) *pitch -= 1.8f * dt;
  *yaw += mouse_dx * 0.0025f;
  *pitch -= mouse_dy * 0.0025f;
  *pitch = clampf(*pitch, -1.35f, 1.35f);
  in.weapon_switch = weapon_switch;
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
                     const char* map_path) {
  if (owned_server) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

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

  SDL_Window* window = SDL_CreateWindow("arena", 1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
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
  SDL_SetWindowRelativeMouseMode(window, true);
  SDL_SetWindowMouseGrab(window, true);
  SDL_SetWindowKeyboardGrab(window, true);
  SDL_RaiseWindow(window);

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
  uint8_t pending_weapon = 0;
  float yaw = 0.0f;
  float pitch = 0.0f;
  bool aim_initialized = false;
  bool map_mismatch_logged = false;
  float hitmarker_timer = 0.0f;
  KillFeedItem kill_feed[4]{};
  Rng fx_rng{0x9e3779b97f4a7c15ull};
  double fps_accum = 0.0;
  int fps_frames = 0;
  float shown_fps = 0.0f;
  double last = app_seconds();
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
    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_EVENT_QUIT || ev.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) running = false;
      if (ev.type == SDL_EVENT_MOUSE_MOTION) {
        mouse_dx += ev.motion.xrel;
        mouse_dy += ev.motion.yrel;
      }
      if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        SDL_SetWindowRelativeMouseMode(window, true);
      }
      if (ev.type == SDL_EVENT_KEY_DOWN && !ev.key.repeat) {
        if (ev.key.key == SDLK_ESCAPE) running = false;
        if (ev.key.key == SDLK_1) pending_weapon = 1;
        if (ev.key.key == SDLK_2) pending_weapon = 2;
      }
    }

    ClientEvents events{};
    client_receive(client, now, &events);
    PlayerInput in = sample_input(pending_weapon, &yaw, &pitch, dt, mouse_dx, mouse_dy);
    pending_weapon = 0;
    client_send_input(client, in);

    GameState view{};
    client_view_state(client, now, &view);
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

    handle_events(events, view, client.player_index, mixer, particles, fx_rng, kill_feed, &hitmarker_timer);
    particles_update(particles, dt);
    if (hitmarker_timer > 0.0f) hitmarker_timer -= dt;
    for (int i = 0; i < 4; ++i) {
      if (kill_feed[i].time_left > 0.0f) kill_feed[i].time_left -= dt;
    }
    int w = 1280;
    int h = 720;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    renderer_begin_frame(renderer, cam, w, h, map);
    renderer_draw_world(renderer);
    for (int i = 0; i < MAX_PLAYERS; ++i) {
      const Player& p = view.players[i];
      if (!p.active || !p.alive) continue;
      if (i == client.player_index) continue;
      float body_h = p.crouching ? PLAYER_CROUCH_HEIGHT : PLAYER_HEIGHT;
      renderer_draw_box(renderer, p.pos + Vec3{0.0f, body_h * 0.5f, 0.0f},
                        {PLAYER_HALF_W * 2.0f, body_h, PLAYER_HALF_W * 2.0f},
                        player_color(i), p.yaw);
      renderer_draw_box(renderer, p.pos + Vec3{0.0f, body_h + 0.18f, 0.0f},
                        {0.36f, 0.36f, 0.36f}, player_color(i) * 1.15f, p.yaw);
    }
    for (int i = 0; i < MAX_ROCKETS; ++i) {
      const Rocket& r = view.rockets[i];
      if (r.active) {
        particles_trail(particles, fx_rng, r.pos);
        renderer_draw_box(renderer, r.pos, {0.18f, 0.18f, 0.55f}, {1.0f, 0.46f, 0.10f}, 0.0f);
      }
    }
    for (int i = 0; i < view.pickup_count; ++i) {
      const Pickup& p = view.pickups[i];
      if (!p.present) continue;
      Vec3 c = p.pos + Vec3{0.0f, 0.45f, 0.0f};
      renderer_draw_box(renderer, c, {0.70f, 0.18f, 0.18f}, {0.18f, 1.00f, 0.35f}, 0.0f);
      renderer_draw_box(renderer, c, {0.18f, 0.70f, 0.18f}, {0.18f, 1.00f, 0.35f}, 0.0f);
    }
    particles_render(particles, renderer, cam);
    uint8_t local_weapon = WEAPON_RIFLE;
    if (client.player_index >= 0 && client.player_index < MAX_PLAYERS &&
        view.players[client.player_index].active) {
      local_weapon = view.players[client.player_index].weapon;
    }
    renderer_end_frame(renderer);

    hud_begin(hud, w, h);
    draw_first_person_hud(hud, w, h, local_weapon);
    draw_status_hud(hud, w, h, client, view, map, kill_feed, hitmarker_timer, shown_fps);
    const bool* keys = SDL_GetKeyboardState(nullptr);
    if (keys[SDL_SCANCODE_TAB]) draw_scoreboard(hud, w, h, view, client.player_index);
    hud_end(hud);
    SDL_GL_SwapWindow(window);
    SDL_Delay(1);
  }

  client_disconnect(client);
  audio_shutdown(mixer);
  SDL_GL_DestroyContext(gl);
  SDL_DestroyWindow(window);
  SDL_Quit();
  if (owned_server) server_thread_stop(*owned_server);
  return 0;
}

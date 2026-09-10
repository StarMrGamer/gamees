#include "tools/headless.h"

#include "client/client.h"
#include "core/rng.h"
#include "game/collision.h"
#include "game/sim.h"
#include "game/tuning.h"
#include "server/server.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

namespace {

void hash_bytes(uint32_t& h, const void* data, size_t len) {
  const uint8_t* p = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < len; ++i) {
    h ^= p[i];
    h *= 16777619u;
  }
}

// Floats are quantised before hashing so that a harmless last-bit difference
// (a different compiler's FMA contraction, say) does not change the digest,
// while any gameplay-visible movement does.
void hash_float(uint32_t& h, float v) {
  int32_t q = static_cast<int32_t>(std::lround(v * 1024.0f));
  hash_bytes(h, &q, sizeof(q));
}

void hash_vec(uint32_t& h, Vec3 v) {
  hash_float(h, v.x);
  hash_float(h, v.y);
  hash_float(h, v.z);
}

// A deterministic "bot" input script. Each player gets its own phase so they
// spread out, chase different headings and fire on different cadences; the
// point is broad, repeatable coverage of the movement and weapon code, not
// good play.
PlayerInput scripted_input(int player, uint32_t tick, uint32_t seq, uint64_t seed) {
  PlayerInput in{};
  in.sequence = seq;
  float t = static_cast<float>(tick) / static_cast<float>(TICK_RATE);
  // The seed shifts every player's phase and cadence, so two seeds really do
  // explore different movement, not just different respawn rolls.
  Rng phase_rng{seed + static_cast<uint64_t>(player) * 0x9E3779B9ull};
  float phase = static_cast<float>(player) * 1.7f + rng_float(phase_rng, 0.0f, 6.28318f);
  uint32_t skew = static_cast<uint32_t>(rng_next(phase_rng) % 17u);

  in.buttons = BTN_FORWARD;
  if (std::sin(t * 0.9f + phase) > 0.55f) in.buttons |= BTN_LEFT;
  if (std::sin(t * 0.7f + phase * 2.0f) < -0.55f) in.buttons |= BTN_RIGHT;
  if ((tick + skew + static_cast<uint32_t>(player) * 7u) % 37u == 0u) in.buttons |= BTN_JUMP;
  if ((tick + skew + static_cast<uint32_t>(player) * 11u) % 53u == 0u) in.buttons |= BTN_AIRJUMP;
  if ((tick + skew + static_cast<uint32_t>(player) * 13u) % 97u == 0u) in.buttons |= BTN_DASH;
  if ((tick + skew + static_cast<uint32_t>(player) * 5u) % 120u < 8u) in.buttons |= BTN_CROUCH;
  if ((tick + skew + static_cast<uint32_t>(player) * 3u) % 19u == 0u) in.buttons |= BTN_FIRE;

  in.yaw = std::sin(t * 0.35f + phase) * PI;
  in.pitch = std::sin(t * 0.21f + phase) * 0.35f;
  in.class_switch = static_cast<uint8_t>(1 + (player % PLAYER_CLASS_COUNT));
  in.weapon_switch = 0;
  return in;
}

}  // namespace

uint32_t sim_state_hash(const GameState& s) {
  uint32_t h = 2166136261u;
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    const Player& p = s.players[i];
    uint8_t flags = static_cast<uint8_t>((p.active ? 1 : 0) | (p.alive ? 2 : 0) |
                                         (p.on_ground ? 4 : 0) | (p.crouching ? 8 : 0) |
                                         (p.sliding ? 16 : 0));
    hash_bytes(h, &flags, sizeof(flags));
    if (!p.active) continue;
    hash_vec(h, p.pos);
    hash_vec(h, p.vel);
    hash_float(h, p.health);
    hash_float(h, p.yaw);
    hash_bytes(h, &p.weapon, sizeof(p.weapon));
    hash_bytes(h, &p.player_class, sizeof(p.player_class));
    hash_bytes(h, &p.frags, sizeof(p.frags));
  }
  for (int i = 0; i < MAX_ROCKETS; ++i) {
    const Rocket& r = s.rockets[i];
    uint8_t active = r.active ? 1 : 0;
    hash_bytes(h, &active, sizeof(active));
    if (r.active) hash_vec(h, r.pos);
  }
  for (int i = 0; i < s.pickup_count; ++i) {
    uint8_t present = s.pickups[i].present ? 1 : 0;
    hash_bytes(h, &present, sizeof(present));
  }
  return h;
}

bool headless_simulate(const char* map_path, const SimOptions& opts, SimReport* out,
                       std::string* error) {
  if (!out) return false;
  Map map{};
  if (!map_load(map_path, &map)) {
    if (error) *error = std::string("failed to load map '") + (map_path ? map_path : "") + "'";
    return false;
  }

  int players = opts.players < 1 ? 1 : (opts.players > MAX_PLAYERS ? MAX_PLAYERS : opts.players);
  int ticks = opts.ticks < 1 ? 1 : opts.ticks;

  GameState state{};
  Rng rng{opts.seed ? opts.seed : 1};
  // A frag limit far out of reach keeps the match from ending (and respawning
  // everyone) part-way through a run, which would muddy the comparison.
  game_init(state, map, 1000000);
  for (int i = 0; i < players; ++i) {
    char name[16];
    std::snprintf(name, sizeof(name), "sim%d", i);
    if (game_player_join(state, map, name) < 0) {
      if (error) *error = "failed to seat synthetic players";
      return false;
    }
  }

  SimReport rep;
  rep.players = players;

  // Wedge detection. Distance travelled is the wrong signal here: a scripted
  // player holding "forward" into a wall legitimately does not move. What is
  // never legitimate is the player's hull ending a tick *inside* solid
  // geometry, so that is what we watch, and only a sustained penetration
  // counts (a single tick can happen mid-resolution).
  int penetrating[MAX_PLAYERS]{};
  float last_y[MAX_PLAYERS]{};
  for (int i = 0; i < players; ++i) last_y[i] = state.players[i].pos.y;
  const int PENETRATION_TICKS = TICK_RATE / 4;  // a quarter second

  std::string trace;
  auto begin = std::chrono::steady_clock::now();

  for (int tick = 0; tick < ticks; ++tick) {
    PlayerInput inputs[MAX_PLAYERS]{};
    for (int i = 0; i < players; ++i) {
      inputs[i] = scripted_input(i, static_cast<uint32_t>(tick), static_cast<uint32_t>(tick + 1),
                                 opts.seed);
      if (inputs[i].buttons & BTN_FIRE) ++rep.shots_fired;
    }
    game_tick(state, map, inputs, rng);

    for (int i = 0; i < players; ++i) {
      const Player& p = state.players[i];
      if (!std::isfinite(p.pos.x) || !std::isfinite(p.pos.y) || !std::isfinite(p.pos.z) ||
          !std::isfinite(p.vel.x) || !std::isfinite(p.vel.y) || !std::isfinite(p.vel.z) ||
          !std::isfinite(p.health)) {
        rep.nan_seen = true;
      }
      float speed = std::sqrt(p.vel.x * p.vel.x + p.vel.z * p.vel.z);
      if (speed > rep.max_speed) rep.max_speed = speed;

      if (p.alive && map_box_overlap(map, player_aabb(p.pos, p.crouching))) {
        if (++penetrating[i] >= PENETRATION_TICKS) {
          rep.stuck_seen = true;
          if (rep.first_stuck_tick == 0) {
            rep.first_stuck_tick = state.tick;
            rep.first_stuck_pos = p.pos;
          }
        }
      } else {
        penetrating[i] = 0;
      }
      // A jump back above the void plane counts as a reset.
      if (last_y[i] < map.void_y && p.pos.y > map.void_y) ++rep.void_resets;
      last_y[i] = p.pos.y;
    }

    if (opts.trace_every > 0 && tick % opts.trace_every == 0) {
      char line[512];
      const Player& p = state.players[0];
      std::snprintf(line, sizeof(line),
                    "{\"tick\":%u,\"p0\":{\"x\":%.3f,\"y\":%.3f,\"z\":%.3f,"
                    "\"vx\":%.3f,\"vy\":%.3f,\"vz\":%.3f,\"hp\":%.1f,\"ground\":%s},"
                    "\"hash\":%u}\n",
                    state.tick, p.pos.x, p.pos.y, p.pos.z, p.vel.x, p.vel.y, p.vel.z,
                    p.health, p.on_ground ? "true" : "false", sim_state_hash(state));
      trace += line;
    }
  }

  auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
  rep.ticks = ticks;
  rep.ticks_per_second = elapsed > 0.0 ? static_cast<double>(ticks) / elapsed : 0.0;
  rep.state_hash = sim_state_hash(state);
  for (int i = 0; i < players; ++i) rep.total_frags += state.players[i].frags;
  rep.trace = trace;
  *out = rep;
  return true;
}

std::string sim_report_json(const SimReport& r) {
  char buf[768];
  std::snprintf(buf, sizeof(buf),
                "{\"ticks\":%d,\"players\":%d,\"state_hash\":%u,\"frags\":%d,"
                "\"shots_fired\":%d,\"void_resets\":%d,\"nan\":%s,\"stuck\":%s,"
                "\"max_speed\":%.3f,\"ticks_per_second\":%.1f}",
                r.ticks, r.players, r.state_hash, r.total_frags, r.shots_fired,
                r.void_resets, r.nan_seen ? "true" : "false", r.stuck_seen ? "true" : "false",
                r.max_speed, r.ticks_per_second);
  return buf;
}

// ---------------------------------------------------------------------------
// Network round-trip check
// ---------------------------------------------------------------------------

bool headless_netcheck(const char* map_path, const NetCheckOptions& opts, NetCheckReport* out,
                       std::string* error) {
  if (!out) return false;
  int client_count = opts.clients < 1 ? 1 : (opts.clients > MAX_PLAYERS - 1 ? MAX_PLAYERS - 1
                                                                            : opts.clients);
  NetCheckReport rep;
  rep.clients_expected = client_count;

  auto server = std::make_unique<Server>();
  if (!server_init(*server, opts.port, map_path, 1000000)) {
    if (error) *error = "failed to start the server (port in use, or map missing)";
    return false;
  }

  NetAddress server_addr{};
  if (!net_address_parse("127.0.0.1", opts.port, &server_addr)) {
    server_shutdown(*server);
    if (error) *error = "failed to resolve the loopback address";
    return false;
  }

  std::vector<std::unique_ptr<Client>> clients;
  for (int i = 0; i < client_count; ++i) {
    auto c = std::make_unique<Client>();
    char name[16];
    std::snprintf(name, sizeof(name), "net%d", i);
    if (!client_start(*c, server_addr, name)) {
      server_shutdown(*server);
      if (error) *error = "failed to open a client socket";
      return false;
    }
    clients.push_back(std::move(c));
  }

  // A virtual clock, stepped one tick at a time. The loop is not sleeping, so
  // this runs far faster than real time while keeping every timestamp the
  // client and server see consistent.
  const int total_ticks = (opts.seconds < 1 ? 1 : opts.seconds) * TICK_RATE;
  double now = 1000.0;
  std::vector<uint32_t> last_snap(clients.size(), 0);

  for (int tick = 0; tick < total_ticks; ++tick) {
    now += TICK_DT;

    for (size_t i = 0; i < clients.size(); ++i) {
      Client& c = *clients[i];
      c.net_now = now;
      PlayerInput in{};
      in.buttons = BTN_FORWARD;
      if ((tick + static_cast<int>(i) * 9) % 41 == 0) in.buttons |= BTN_JUMP;
      in.yaw = std::sin(static_cast<float>(tick) * 0.03f + static_cast<float>(i)) * PI;
      client_send_input(c, in);
    }

    server_pump(*server, now);
    server_tick(*server);
    server_broadcast(*server);

    for (size_t i = 0; i < clients.size(); ++i) {
      Client& c = *clients[i];
      client_receive(c, now, nullptr);
      if (c.newest_snap_tick > last_snap[i]) {
        ++rep.snapshots_received;
        last_snap[i] = c.newest_snap_tick;
      }
    }
  }

  rep.server_ticks = server->state.tick;
  for (size_t i = 0; i < clients.size(); ++i) {
    const Client& c = *clients[i];
    if (c.state == CLIENT_CONNECTED && c.player_index >= 0) {
      ++rep.clients_connected;
      // Compare what the client renders for itself against the server's
      // authoritative position: that difference is the prediction error.
      GameState view;
      client_view_state(c, &view);
      const Player& predicted = view.players[c.player_index];
      const Player& authoritative = server->state.players[c.player_index];
      if (predicted.active && authoritative.active) {
        Vec3 d = predicted.pos - authoritative.pos;
        float err = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        if (err > rep.max_prediction_error) rep.max_prediction_error = err;
      }
    }
  }

  for (auto& c : clients) client_disconnect(*c);
  server_shutdown(*server);

  if (rep.clients_connected != rep.clients_expected) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "only %d of %d clients connected",
                  rep.clients_connected, rep.clients_expected);
    rep.failure = buf;
  } else if (rep.snapshots_received < static_cast<uint32_t>(total_ticks / 2)) {
    rep.failure = "snapshots did not flow at the expected rate";
  } else if (rep.max_prediction_error > 2.0f) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "prediction diverged by %.2f m",
                  static_cast<double>(rep.max_prediction_error));
    rep.failure = buf;
  } else {
    rep.ok = true;
  }
  *out = rep;
  return true;
}

std::string netcheck_report_json(const NetCheckReport& r) {
  char buf[512];
  std::snprintf(buf, sizeof(buf),
                "{\"ok\":%s,\"clients_connected\":%d,\"clients_expected\":%d,"
                "\"server_ticks\":%u,\"snapshots_received\":%u,"
                "\"max_prediction_error\":%.3f,\"failure\":\"%s\"}",
                r.ok ? "true" : "false", r.clients_connected, r.clients_expected,
                r.server_ticks, r.snapshots_received,
                static_cast<double>(r.max_prediction_error), r.failure.c_str());
  return buf;
}

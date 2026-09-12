#include "tools/headless.h"

#include "ai/agent.h"
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
  auto map_storage = std::make_unique<Map>();
  Map& map = *map_storage;
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

namespace {

const char* agent_name(int kind, int skill) {
  if (kind != AGENT_DEMON) return "simple";
  return agent_skill_name(static_cast<AgentSkill>(skill));
}

}  // namespace

bool headless_eval(const char* map_path, int a_kind, int b_kind, const EvalOptions& opts,
                   EvalReport* out, std::string* error) {
  if (!out) return false;
  auto map_storage = std::make_unique<Map>();
  Map& map = *map_storage;
  if (!map_load(map_path, &map)) {
    if (error) *error = std::string("failed to load map '") + (map_path ? map_path : "") + "'";
    return false;
  }
  // Bots path over this; nothing else in the engine needs it, so it is built
  // here rather than in map_load().
  bool r_nav_built = false;
  int r_nav_nodes = 0;
  map_build_nav(&map);
  r_nav_built = map.nav.built;
  r_nav_nodes = map.nav.node_count;
  int per_side = opts.per_side < 1 ? 1 : opts.per_side;
  if (per_side * 2 > MAX_PLAYERS) per_side = MAX_PLAYERS / 2;
  const int count = per_side * 2;

  EvalReport r;
  r.nav_built = r_nav_built;
  r.nav_nodes = r_nav_nodes;
  r.matches = opts.matches;
  r.a_name = agent_name(a_kind, opts.skill_a);
  r.b_name = agent_name(b_kind, opts.skill_b);

  auto state = std::make_unique<GameState>();
  std::vector<AgentMemory> mem(static_cast<size_t>(count));
  // Skill picks the tier; handicap then scales within it, so a ladder can be
  // built either coarsely or continuously.
  AgentConfig cfg_a = agent_config(static_cast<AgentSkill>(opts.skill_a));
  AgentConfig cfg_b = agent_config(static_cast<AgentSkill>(opts.skill_b));
  if (opts.handicap_a > 0.0f) cfg_a = agent_config_demon_handicapped(opts.handicap_a);
  if (opts.handicap_b > 0.0f) cfg_b = agent_config_demon_handicapped(opts.handicap_b);

  long long total_ticks = 0;
  auto t0 = std::chrono::steady_clock::now();

  for (int match = 0; match < opts.matches; ++match) {
    // Sides swap every other match so a map's spawn layout cannot hand one
    // agent an advantage that looks like skill.
    const bool swapped = (match & 1) != 0;
    Rng rng{opts.seed + static_cast<uint64_t>(match) * 7919ull};
    game_init(*state, map, opts.frag_limit);
    for (int i = 0; i < count; ++i) {
      char name[16];
      std::snprintf(name, sizeof(name), "%c%d", i < per_side ? 'a' : 'b', i);
      game_player_join(*state, map, name);
      agent_reset(mem[static_cast<size_t>(i)]);
    }

    auto side_of = [&](int i) { return i < per_side ? 0 : 1; };
    auto kind_of = [&](int i) {
      int side = side_of(i);
      if (swapped) side = 1 - side;
      return side == 0 ? a_kind : b_kind;
    };
    auto cfg_of = [&](int i) -> const AgentConfig& {
      int side = side_of(i);
      if (swapped) side = 1 - side;
      return side == 0 ? cfg_a : cfg_b;
    };

    int frags[2] = {0, 0};
    int deaths[2] = {0, 0};
    double speed_sum[2] = {0.0, 0.0};
    long long speed_samples[2] = {0, 0};
    float top_speed[2] = {0.0f, 0.0f};
    int airborne[2] = {0, 0};
    int shots[2] = {0, 0};
    int void_falls[2] = {0, 0};
    float prev_health[MAX_PLAYERS];
    bool prev_alive[MAX_PLAYERS];
    bool prev_in_void[MAX_PLAYERS];
    for (int i = 0; i < count; ++i) {
      prev_health[i] = state->players[i].health;
      prev_alive[i] = state->players[i].alive;
      prev_in_void[i] = false;
    }
    double damage[2] = {0.0, 0.0};
    int tick = 0;
    bool decided = false;

    for (; tick < opts.max_ticks; ++tick) {
      PlayerInput inputs[MAX_PLAYERS]{};
      for (int i = 0; i < count; ++i) {
        inputs[i] = agent_think(*state, map, i, static_cast<AgentKind>(kind_of(i)), cfg_of(i),
                                mem[static_cast<size_t>(i)], rng, TICK_DT);
        inputs[i].sequence = static_cast<uint32_t>(tick + 1);
        if (inputs[i].buttons & BTN_FIRE) ++shots[side_of(i)];
      }
      game_tick(*state, map, inputs, rng);
      ++total_ticks;

      for (int i = 0; i < count; ++i) {
        const Player& p = state->players[i];
        if (!std::isfinite(p.pos.x) || !std::isfinite(p.pos.y) || !std::isfinite(p.pos.z)) {
          r.nan_seen = true;
        }
        // Falling out is counted per event, not per tick: the void plane
        // teleports the player back, so a single fall would otherwise be
        // scored dozens of times on the way down.
        bool in_void = p.pos.y < map.void_y + 0.5f;
        if (in_void && !prev_in_void[i]) ++void_falls[side_of(i)];
        prev_in_void[i] = in_void;

        // Deaths are alive->dead transitions. Frags do not cover it: a bot that
        // walks into the void dies without anyone scoring.
        if (prev_alive[i] && !p.alive) ++deaths[side_of(i)];
        prev_alive[i] = p.alive;

        // Damage is credited to the other side. Self-inflicted splash is
        // misattributed by this, which is worth remembering before reading too
        // much into the column.
        if (p.health < prev_health[i] && p.alive) damage[1 - side_of(i)] += prev_health[i] - p.health;
        prev_health[i] = p.health;

        if (p.alive) {
          float sp = std::sqrt(p.vel.x * p.vel.x + p.vel.z * p.vel.z);
          speed_sum[side_of(i)] += sp;
          ++speed_samples[side_of(i)];
          if (sp > top_speed[side_of(i)]) top_speed[side_of(i)] = sp;
          if (!p.on_ground) ++airborne[side_of(i)];
        }
      }

      // Team score is the sum of its members' frags.
      int score[2] = {0, 0};
      for (int i = 0; i < count; ++i) score[side_of(i)] += state->players[i].frags;
      frags[0] = score[0];
      frags[1] = score[1];
      if (state->match_over || score[0] >= opts.frag_limit || score[1] >= opts.frag_limit) {
        decided = true;
        break;
      }
    }

    // Map team slots back to agents, undoing the swap.
    int agent_of_side[2] = {swapped ? 1 : 0, swapped ? 0 : 1};
    EvalSideStats* stats[2] = {&r.a, &r.b};
    for (int side = 0; side < 2; ++side) {
      EvalSideStats& st = *stats[agent_of_side[side]];
      st.frags += frags[side];
      st.deaths += deaths[side];
      st.shots += shots[side];
      st.void_falls += void_falls[side];
      st.damage_dealt += damage[side];
      st.speed_sum += speed_sum[side];
      st.speed_samples += speed_samples[side];
      st.airborne_ticks += airborne[side];
      if (top_speed[side] > st.top_speed) st.top_speed = top_speed[side];
    }
    // A match that runs out of clock is not a draw - it goes to whoever was
    // ahead, as it would in any real ruleset. Scoring timeouts as draws
    // reported 96 frags against -227 as an even result.
    (void)decided;
    if (frags[0] == frags[1]) {
      ++r.draws;
    } else {
      int winner = frags[0] > frags[1] ? 0 : 1;
      ++stats[agent_of_side[winner]]->wins;
    }
    r.avg_match_seconds += static_cast<double>(tick) * TICK_DT;
  }

  double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  if (opts.matches > 0) r.avg_match_seconds /= opts.matches;
  r.ticks_per_second = secs > 0.0 ? static_cast<double>(total_ticks) / secs : 0.0;
  *out = r;
  return true;
}

std::string eval_report_json(const EvalReport& r) {
  char buf[768];
  std::snprintf(buf, sizeof(buf),
                "{\"matches\":%d,\"draws\":%d,\"a\":{\"name\":\"%s\",\"wins\":%d,"
                "\"frags\":%d,\"deaths\":%d,\"shots\":%d,\"void_falls\":%d,\"damage\":%.0f},"
                "\"b\":{\"name\":\"%s\",\"wins\":%d,\"frags\":%d,\"deaths\":%d,\"shots\":%d,"
                "\"void_falls\":%d,\"damage\":%.0f},"
                "\"a_speed\":%.2f,\"a_top_speed\":%.1f,\"b_speed\":%.2f,\"b_top_speed\":%.1f,"
                "\"nav_built\":%s,\"nav_nodes\":%d,"
                "\"avg_match_seconds\":%.1f,\"ticks_per_second\":%.0f,\"nan\":%s}",
                r.matches, r.draws, r.a_name, r.a.wins, r.a.frags, r.a.deaths, r.a.shots,
                r.a.void_falls, r.a.damage_dealt, r.b_name, r.b.wins, r.b.frags, r.b.deaths,
                r.b.shots, r.b.void_falls, r.b.damage_dealt,
                r.a.speed_samples ? r.a.speed_sum / r.a.speed_samples : 0.0, r.a.top_speed,
                r.b.speed_samples ? r.b.speed_sum / r.b.speed_samples : 0.0, r.b.top_speed,
                r.nav_built ? "true" : "false", r.nav_nodes,
                r.avg_match_seconds, r.ticks_per_second,
                r.nan_seen ? "true" : "false");
  return buf;
}

std::string eval_report_text(const EvalReport& r) {
  char buf[1024];
  double decided = static_cast<double>(r.matches - r.draws);
  double win_a = decided > 0 ? 100.0 * r.a.wins / decided : 0.0;
  std::snprintf(buf, sizeof(buf),
                "%d matches: %s %d - %d %s (%d draws)\n"
                "  %-7s win %5.1f%%  frags %5d  deaths %5d  damage %8.0f  shots %6d  fell out %d\n"
                "  %-7s win %5.1f%%  frags %5d  deaths %5d  damage %8.0f  shots %6d  fell out %d\n"
                "  %-7s speed avg %4.1f m/s  top %5.1f m/s  airborne %4.1f%%\n"
                "  %-7s speed avg %4.1f m/s  top %5.1f m/s  airborne %4.1f%%\n"
                "  navmesh %s (%d nodes)\n"
                "  average match %.1f s, %.0f sim ticks/s (%.0fx realtime)",
                r.matches, r.a_name, r.a.wins, r.b.wins, r.b_name, r.draws,
                r.a_name, win_a, r.a.frags, r.a.deaths, r.a.damage_dealt, r.a.shots, r.a.void_falls,
                r.b_name, 100.0 - win_a, r.b.frags, r.b.deaths, r.b.damage_dealt, r.b.shots, r.b.void_falls,
                r.a_name, r.a.speed_samples ? r.a.speed_sum / r.a.speed_samples : 0.0,
                r.a.top_speed,
                r.a.speed_samples ? 100.0 * r.a.airborne_ticks / r.a.speed_samples : 0.0,
                r.b_name, r.b.speed_samples ? r.b.speed_sum / r.b.speed_samples : 0.0,
                r.b.top_speed,
                r.b.speed_samples ? 100.0 * r.b.airborne_ticks / r.b.speed_samples : 0.0,
                r.nav_built ? "built" : "UNAVAILABLE - bots steer straight", r.nav_nodes,
                r.avg_match_seconds, r.ticks_per_second, r.ticks_per_second / TICK_RATE);
  return buf;
}

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

// ---------------------------------------------------------------------------
// Point probe
// ---------------------------------------------------------------------------

namespace {

// Must mirror move_slide's own rule exactly, or the probe reports a position as
// stuck that the simulation is perfectly happy with.
bool probe_inside_solid(const Map& map, Vec3 pos) {
  if (map_box_overlap(map, player_aabb(pos, false))) return true;
  return map_ramp_blocks(map, pos, false);
}

}  // namespace

bool probe_position(const char* map_path, Vec3 pos, ProbeReport* out, std::string* error) {
  if (!out) return false;
  auto map_storage = std::make_unique<Map>();
  Map& map = *map_storage;
  if (!map_load(map_path, &map)) {
    if (error) *error = std::string("failed to load map '") + (map_path ? map_path : "") + "'";
    return false;
  }

  ProbeReport r;
  r.pos = pos;
  r.void_y = map.void_y;
  r.inside_solid = probe_inside_solid(map, pos);

  // Ask the engine rather than re-deriving it. This used to run its own copy
  // of the test with map_ramp_surface(), which reports the highest ramp
  // anywhere in the column - on stacked terrain that is a hillside overhead,
  // not the ground underfoot - so the probe could disagree with the movement
  // code about whether a spot is standable.
  r.grounded = map_grounded_at(map, pos, false);
  r.standable = !r.inside_solid && r.grounded;

  // Rays are cast from eye height so a surface the feet are resting on does not
  // register as zero distance.
  Vec3 eye = pos;
  eye.y += EYE_HEIGHT;
  float down = ray_map(map, eye, {0.0f, -1.0f, 0.0f}, 500.0f);
  r.has_floor = down < 500.0f;
  r.floor_distance = r.has_floor ? std::max(0.0f, down - EYE_HEIGHT) : -1.0f;
  float up = ray_map(map, eye, {0.0f, 1.0f, 0.0f}, 500.0f);
  r.ceiling_distance = up < 500.0f ? up : -1.0f;

  // Drop test with the real physics: does this spot lead out of the world?
  {
    Vec3 p = pos;
    Vec3 v{0.0f, 0.0f, 0.0f};
    r.falls_out = true;
    for (int i = 0; i < 6 * TICK_RATE; ++i) {
      v.y -= GRAVITY * TICK_DT;
      MoveResult mr = move_slide(map, p, v, false, TICK_DT);
      p = mr.pos;
      v = mr.vel;
      if (p.y < map.void_y) { r.falls_out = true; break; }
      if (mr.on_ground) { r.falls_out = false; break; }
    }
    r.rest_y = p.y;
  }

  if (r.inside_solid) {
    for (float lift = 0.005f; lift <= 1.0f; lift += 0.005f) {
      Vec3 up_pos{pos.x, pos.y + lift, pos.z};
      if (!probe_inside_solid(map, up_pos)) {
        r.penetration = lift;
        break;
      }
    }
  }

  // When embedded, find the closest direction back out - that is what you need
  // to know to judge whether it is a thin seam or a solid block.
  if (r.inside_solid) {
    const Vec3 dirs[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (float dist = 0.1f; dist <= 6.0f && r.escape_distance < 0.0f; dist += 0.1f) {
      for (const Vec3& d : dirs) {
        Vec3 candidate{pos.x + d.x * dist, pos.y + d.y * dist, pos.z + d.z * dist};
        if (!probe_inside_solid(map, candidate)) {
          r.escape_distance = dist;
          r.escape_dir = d;
          break;
        }
      }
    }
  }

  for (int i = 0; i < map.spawn_count; ++i) {
    Vec3 d = map.spawns[i] - pos;
    float dist = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    if (r.nearest_spawn < 0.0f || dist < r.nearest_spawn) r.nearest_spawn = dist;
  }

  const float radius = 3.0f;
  auto near_box = [&](Vec3 mn, Vec3 mx) {
    return pos.x > mn.x - radius && pos.x < mx.x + radius && pos.y > mn.y - radius &&
           pos.y < mx.y + radius && pos.z > mn.z - radius && pos.z < mx.z + radius;
  };
  for (int i = 0; i < map.box_count; ++i) {
    if (near_box(map.boxes[i].min, map.boxes[i].max)) ++r.boxes_near;
  }
  for (int i = 0; i < map.ramp_count; ++i) {
    if (near_box(map.ramps[i].min, map.ramps[i].max)) ++r.ramps_near;
  }
  for (int i = 0; i < map.brush_count; ++i) {
    if (near_box(map.brushes[i].min, map.brushes[i].max)) ++r.brushes_near;
  }

  *out = r;
  return true;
}

std::string probe_report_text(const ProbeReport& r) {
  char buf[1400];
  char floor_s[48];
  char ceil_s[48];
  char escape_s[80];
  if (r.has_floor) std::snprintf(floor_s, sizeof(floor_s), "%.2f m below", r.floor_distance);
  else std::snprintf(floor_s, sizeof(floor_s), "NOTHING BELOW");
  if (r.ceiling_distance >= 0.0f) std::snprintf(ceil_s, sizeof(ceil_s), "%.2f m above", r.ceiling_distance);
  else std::snprintf(ceil_s, sizeof(ceil_s), "open sky");
  if (r.inside_solid && r.penetration > 0.0f && r.penetration <= 0.05f) {
    std::snprintf(escape_s, sizeof(escape_s), "clears with a %.0f mm lift",
                  r.penetration * 1000.0f);
  } else if (r.inside_solid && r.escape_distance >= 0.0f) {
    std::snprintf(escape_s, sizeof(escape_s), "nearest free spot %.1f m along (%.0f %.0f %.0f)",
                  r.escape_distance, r.escape_dir.x, r.escape_dir.y, r.escape_dir.z);
  } else if (r.inside_solid) {
    std::snprintf(escape_s, sizeof(escape_s), "no free spot within 6 m - deeply buried");
  } else {
    escape_s[0] = 0;
  }
  std::snprintf(buf, sizeof(buf),
                "position      %.2f %.2f %.2f\n"
                "  inside solid  %s%s%s\n"
                "  standable     %s (grounded %s)\n"
                "  floor         %s\n"
                "  ceiling       %s\n"
                "  drop test     %s (rest y %.2f, void at %.2f)\n"
                "  nearest spawn %.1f m\n"
                "  geometry <3m  %d boxes, %d ramps, %d brushes\n",
                r.pos.x, r.pos.y, r.pos.z,
                !r.inside_solid ? "no"
                    : (r.penetration > 0.0f && r.penetration <= 0.05f)
                          ? "touching - resting on a surface, not trapped"
                          : "YES - player is stuck here",
                escape_s[0] ? "\n    " : "", escape_s,
                r.standable ? "yes" : "no", r.grounded ? "yes" : "no",
                floor_s, ceil_s,
                r.falls_out ? "FALLS OUT OF THE WORLD" : "lands safely",
                r.rest_y, r.void_y, r.nearest_spawn,
                r.boxes_near, r.ramps_near, r.brushes_near);
  return buf;
}

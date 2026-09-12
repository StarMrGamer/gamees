#pragma once

#include "game/game_state.h"
#include "game/map.h"
#include "core/math.h"

#include <cstdint>
#include <string>

// Headless harnesses for agents and CI: no SDL, no OpenGL, no sockets. These
// drive the shared simulation directly so a change to physics, weapons or the
// tick loop can be measured and diffed from a terminal.

struct SimOptions {
  int ticks = 600;         // how long to run (60 ticks == 1 second)
  int players = 4;         // synthetic players to spawn
  uint64_t seed = 12345;   // input script + sim RNG seed
  int trace_every = 0;     // emit a trace sample every N ticks (0 = final only)
  bool json = true;
};

struct SimReport {
  int ticks = 0;
  int players = 0;
  uint32_t state_hash = 0;   // fingerprint of the final simulation state
  int total_frags = 0;
  int total_deaths = 0;
  int shots_fired = 0;
  int void_resets = 0;
  bool nan_seen = false;
  bool stuck_seen = false;   // a player's hull sat inside solid geometry
  uint32_t first_stuck_tick = 0;
  Vec3 first_stuck_pos{};
  float max_speed = 0.0f;
  double ticks_per_second = 0.0;
  std::string trace;         // JSON lines, one object per sampled tick
};

// Runs `opts.ticks` of the authoritative simulation with scripted inputs.
// Deterministic: the same map + seed always produces the same state_hash, so
// an agent can diff a hash before and after a change to see whether gameplay
// moved at all.
bool headless_simulate(const char* map_path, const SimOptions& opts, SimReport* out,
                       std::string* error);

// Stable fingerprint of a game state (positions, health, frags, rockets).
uint32_t sim_state_hash(const GameState& s);

std::string sim_report_json(const SimReport& r);

// End-to-end check of the live network path - the one thing headless_simulate
// cannot cover, because it drives the simulation directly rather than through
// the protocol. Runs a real server and real clients over UDP loopback in one
// process and asserts the round trip works: clients are accepted, snapshots
// flow, and the client's prediction stays close to the server's authoritative
// state.
struct NetCheckOptions {
  int clients = 3;
  int seconds = 3;
  uint16_t port = 28150;
};

struct NetCheckReport {
  int clients_connected = 0;
  int clients_expected = 0;
  uint32_t server_ticks = 0;
  uint32_t snapshots_received = 0;   // summed across clients
  float max_prediction_error = 0.0f; // metres, worst client
  bool ok = false;
  std::string failure;
};

bool headless_netcheck(const char* map_path, const NetCheckOptions& opts, NetCheckReport* out,
                       std::string* error);

std::string netcheck_report_json(const NetCheckReport& r);

// Bot-vs-bot evaluation. Runs whole matches headlessly with each side driven
// by an AgentKind, and reports who won. This is the scoreboard: "the new bot
// feels better" is not a claim, "it wins 98.4% of 400 matches" is.
struct EvalOptions {
  int matches = 100;
  int per_side = 1;          // bots per side (1 = duel)
  int frag_limit = 15;
  int max_ticks = 60 * 180;  // give up on a match after three minutes
  uint64_t seed = 9001;
  float handicap_a = 0.0f;   // 0 = full strength, 1 = maximally handicapped
  float handicap_b = 0.0f;
};

struct EvalSideStats {
  int wins = 0;
  int frags = 0;
  int deaths = 0;
  int shots = 0;
  int void_falls = 0;        // walked or fell out of the world
  double damage_dealt = 0.0;
  // Movement quality. GROUND_MAX_SPEED is 8 m/s, so anything above that is the
  // bot actually using dashes, slides and air control rather than walking.
  double speed_sum = 0.0;
  long long speed_samples = 0;
  float top_speed = 0.0f;
  int airborne_ticks = 0;
};

struct EvalReport {
  int matches = 0;
  int draws = 0;             // finished dead level on frags
  EvalSideStats a, b;
  const char* a_name = "";
  const char* b_name = "";
  double avg_match_seconds = 0.0;
  double ticks_per_second = 0.0;
  bool nan_seen = false;
  // Reported because a silent fallback to direct steering changes the result
  // completely, and a run that quietly lost its navmesh should say so.
  bool nav_built = false;
  int nav_nodes = 0;
};

// `a_kind`/`b_kind` are AgentKind values (see ai/agent.h); taken as int here so
// this header does not drag the AI in.
bool headless_eval(const char* map_path, int a_kind, int b_kind, const EvalOptions& opts,
                   EvalReport* out, std::string* error);

std::string eval_report_json(const EvalReport& r);
std::string eval_report_text(const EvalReport& r);

// Answers "what does the engine think is at this spot?" for a single position -
// the counterpart to the client's P key, which copies a position to the
// clipboard in exactly the format --probe accepts.
struct ProbeReport {
  Vec3 pos{};
  bool inside_solid = false;   // the player hull overlaps geometry here
  // How far straight up the hull must move to come free. Standing exactly on a
  // surface overlaps it by a fraction of a millimetre, which is resting contact
  // rather than being trapped; only a real burial needs a meaningful lift.
  float penetration = 0.0f;
  bool standable = false;      // hull is clear and something supports it
  bool grounded = false;
  bool has_floor = false;
  float floor_distance = -1.0f;   // metres down to the first surface
  float ceiling_distance = -1.0f;
  bool falls_out = false;      // dropping from here leaves the world
  float rest_y = 0.0f;         // where a drop from here comes to rest
  float escape_distance = -1.0f;  // nearest free spot, when embedded
  Vec3 escape_dir{};
  float nearest_spawn = -1.0f;
  int boxes_near = 0, ramps_near = 0, brushes_near = 0;
  float void_y = 0.0f;
};

bool probe_position(const char* map_path, Vec3 pos, ProbeReport* out, std::string* error);
std::string probe_report_text(const ProbeReport& r);

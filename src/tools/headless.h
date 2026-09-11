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

// Answers "what does the engine think is at this spot?" for a single position -
// the counterpart to the client's P key, which copies a position to the
// clipboard in exactly the format --probe accepts.
struct ProbeReport {
  Vec3 pos{};
  bool inside_solid = false;   // the player hull overlaps geometry here
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

#pragma once

#include "game/game_state.h"

#include <cstdint>

// How many ticks of player poses the server keeps for lag compensation.
constexpr int HISTORY_TICKS = 90;  // 1.5 s at 60 Hz

// A player's hit-test pose at one tick. Only the fields hitscan collision needs
// are stored, so the whole history stays small enough to live inside Server.
struct PlayerPose {
  Vec3 pos;
  bool crouching;
  bool alive;
  bool active;
};

// One tick's worth of poses. A tick of 0 marks an unused (never recorded) slot,
// which is safe because the simulation's tick counter starts at 1.
struct HistorySample {
  uint32_t tick;
  PlayerPose players[MAX_PLAYERS];
};

// Tick-indexed ring of recent player poses, recorded once per server tick
// immediately after the simulation advances. Because every tick is recorded,
// slot `tick % HISTORY_TICKS` holds that tick's poses and a lookup is a direct
// index rather than a scan.
struct History {
  HistorySample samples[HISTORY_TICKS];
  uint32_t newest_tick;  // 0 until the first record
};

void history_record(History& h, const GameState& s);

// Copy the newest recorded pose sample at or before `target_tick` into `out`,
// provided it is no more than `max_rewind` ticks behind the newest recorded
// tick. Returns false (leaving `out` untouched) when no usable sample exists,
// which tells the caller to fall back to live (un-rewound) hit detection.
bool history_lookup(const History& h, uint32_t target_tick, uint32_t max_rewind,
                    PlayerPose out[MAX_PLAYERS]);

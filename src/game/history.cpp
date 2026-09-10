#include "game/history.h"

#include <cstring>

void history_record(History& h, const GameState& s) {
  if (s.tick > h.newest_tick) h.newest_tick = s.tick;
  HistorySample& sample = h.samples[s.tick % HISTORY_TICKS];
  sample.tick = s.tick;
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    const Player& p = s.players[i];
    PlayerPose& pose = sample.players[i];
    pose.pos = p.pos;
    pose.crouching = p.crouching;
    pose.alive = p.alive;
    pose.active = p.active;
  }
}

bool history_lookup(const History& h, uint32_t target_tick, uint32_t max_rewind,
                    PlayerPose out[MAX_PLAYERS]) {
  if (target_tick == 0 || h.newest_tick == 0) return false;

  // A client claiming a tick the server has not reached yet gets the newest
  // sample rather than being refused; clock skew of a tick or two is normal.
  uint32_t want = target_tick < h.newest_tick ? target_tick : h.newest_tick;
  if (h.newest_tick - want > max_rewind) return false;

  // Every tick is recorded, so the slot for `want` either holds it or the ring
  // has already wrapped past it. Walking back covers a gap from a stalled
  // server without ever scanning the whole ring.
  uint32_t oldest = h.newest_tick > static_cast<uint32_t>(HISTORY_TICKS - 1)
                        ? h.newest_tick - static_cast<uint32_t>(HISTORY_TICKS - 1)
                        : 1u;
  for (uint32_t t = want; t >= oldest; --t) {
    const HistorySample& sample = h.samples[t % HISTORY_TICKS];
    if (sample.tick == t) {
      if (h.newest_tick - t > max_rewind) return false;
      std::memcpy(out, sample.players, sizeof(sample.players));
      return true;
    }
    if (t == 0) break;  // guard the unsigned wrap
  }
  return false;
}

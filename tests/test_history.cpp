#include "test_harness.h"

#include "game/history.h"

TEST(history_records_and_looks_up_poses) {
  History h{};
  GameState s{};
  s.tick = 10;
  s.players[2].active = true;
  s.players[2].alive = true;
  s.players[2].pos = {1, 2, 3};
  history_record(h, s);
  CHECK_EQ_INT(h.samples[10 % HISTORY_TICKS].tick, 10);

  s.tick = 20;
  s.players[2].pos = {4, 5, 6};
  s.players[2].crouching = true;
  history_record(h, s);

  PlayerPose out[MAX_PLAYERS]{};
  CHECK(history_lookup(h, 20, 60, out));
  CHECK_NEAR(out[2].pos.x, 4.0f, 0.0001f);
  CHECK(out[2].crouching);

  CHECK(history_lookup(h, 10, 60, out));
  CHECK_NEAR(out[2].pos.x, 1.0f, 0.0001f);
  CHECK(!out[2].crouching);
}

TEST(history_respects_max_rewind) {
  History h{};
  GameState s{};
  s.players[0].active = true;
  s.players[0].alive = true;
  for (uint32_t t : {5u, 50u, 100u}) {
    s.tick = t;
    history_record(h, s);
  }

  PlayerPose out[MAX_PLAYERS]{};
  CHECK(history_lookup(h, 50, 60, out));   // 50 ticks behind newest: allowed
  CHECK(history_lookup(h, 70, 60, out));   // resolves to tick 50, still in window
  CHECK(!history_lookup(h, 5, 60, out));   // 95 ticks behind newest: rejected
}

TEST(history_wraps_and_rejects_unknown_tick) {
  History h{};
  GameState s{};
  s.players[0].active = true;
  s.players[0].alive = true;
  for (uint32_t t = 1; t <= static_cast<uint32_t>(2 * HISTORY_TICKS); ++t) {
    s.tick = t;
    s.players[0].pos = {static_cast<float>(t), 0, 0};
    history_record(h, s);
  }

  PlayerPose out[MAX_PLAYERS]{};
  CHECK(history_lookup(h, static_cast<uint32_t>(2 * HISTORY_TICKS), 60, out));
  CHECK_NEAR(out[0].pos.x, static_cast<float>(2 * HISTORY_TICKS), 0.001f);
  // Tick 1 has been overwritten by the ring, and tick 0 is always "unknown".
  CHECK(!history_lookup(h, 1, 1000, out));
  CHECK(!history_lookup(h, 0, 60, out));
}

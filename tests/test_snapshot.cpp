#include "test_harness.h"

#include "game/snapshot.h"
#include "game/tuning.h"

#include <cstring>

TEST(snapshot_roundtrip_and_interpolate) {
  GameState s{};
  s.tick = 42;
  s.frag_limit = 20;
  s.next_event_id = 1;
  s.players[0].active = true;
  s.players[0].alive = true;
  std::strcpy(s.players[0].name, "p0");
  s.players[0].pos = {1, 2, 3};
  s.players[0].health = 91;
  s.players[0].frags = 3;
  s.pickup_count = 1;
  s.pickups[0].present = true;
  s.pickups[0].pos = {4, 0, 4};

  uint8_t buf[MAX_PACKET];
  NetWriter w;
  nw_init(w, buf, sizeof(buf));
  snapshot_write(s, w);
  CHECK(!w.overflow);

  GameState read{};
  NetReader r;
  nr_init(r, buf, w.len);
  CHECK(snapshot_read(read, r));
  CHECK_EQ_INT(read.tick, 42);
  CHECK(read.players[0].active);
  CHECK_NEAR(read.players[0].pos.z, 3.0f, 0.0001f);

  GameState b = read;
  b.players[0].pos = {3, 2, 3};
  GameState out{};
  snapshot_interpolate(read, b, 0.5f, out);
  CHECK_NEAR(out.players[0].pos.x, 2.0f, 0.0001f);
}

TEST(snapshot_worst_case_fits) {
  GameState s{};
  s.tick = 100;
  s.frag_limit = 20;
  s.next_event_id = 1;
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    Player& p = s.players[i];
    p.active = true;
    p.alive = true;
    std::snprintf(p.name, sizeof(p.name), "player%d", i);
    p.pos = {static_cast<float>(i), 1, 0};
    p.vel = {1, 2, 3};
    p.health = 100;
    p.frags = i;
  }
  for (int i = 0; i < MAX_ROCKETS; ++i) {
    s.rockets[i].active = true;
    s.rockets[i].pos = {static_cast<float>(i), 2, 3};
    s.rockets[i].vel = {1, 0, 0};
  }
  s.pickup_count = MAX_PICKUPS;
  for (int i = 0; i < MAX_PICKUPS; ++i) {
    s.pickups[i].present = true;
    s.pickups[i].pos = {0, 0, static_cast<float>(i)};
  }
  for (int i = 0; i < MAX_EVENTS; ++i) {
    GameEvent& e = s.events[i];
    e.id = static_cast<uint32_t>(i + 1);
    e.type = EV_SOUND;
    e.a = SND_RIFLE;
    e.pos = {1, 2, 3};
    e.tick = 100;
  }

  uint8_t buf[MAX_PACKET];
  NetWriter w;
  nw_init(w, buf, sizeof(buf));
  packet_header_write(w, PKT_SV_SNAPSHOT);
  snapshot_write(s, w);
  CHECK(!w.overflow);
  CHECK(w.len < MAX_PACKET);
}

TEST(snapshot_truncation_fails) {
  GameState s{};
  s.tick = 1;
  s.frag_limit = 20;
  uint8_t buf[MAX_PACKET];
  NetWriter w;
  nw_init(w, buf, sizeof(buf));
  snapshot_write(s, w);
  GameState out{};
  NetReader r;
  nr_init(r, buf, w.len - 1);
  CHECK(!snapshot_read(out, r));
}

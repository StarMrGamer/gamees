#include "test_harness.h"

#include "game/movement.h"
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
  s.players[0].player_class = CLASS_SCOUT;
  s.players[0].frags = 3;
  s.players[0].stamina = 2;
  s.players[0].stamina_recharge_timer = 0.75f;
  s.players[0].slide_time = 0.5f;
  s.players[0].jump_buffer = 0.05f;
  s.players[0].wall_normal = {1, 0, 0};
  s.players[0].wall_contact_time = 0.08f;
  s.players[0].wall_jump_cooldown = 0.2f;
  s.players[0].dash_air_control_time = 0.15f;
  s.players[0].jump_held = true;
  s.players[0].air_jump_used = true;
  s.players[0].slide_suppressed = true;
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
  CHECK_EQ_INT(read.players[0].player_class, CLASS_SCOUT);
  CHECK_EQ_INT(read.players[0].stamina, 2);
  CHECK_NEAR(read.players[0].stamina_recharge_timer, 0.75f, 0.0001f);
  CHECK_NEAR(read.players[0].slide_time, 0.5f, 0.0001f);
  CHECK_NEAR(read.players[0].jump_buffer, 0.05f, 0.0001f);
  CHECK_NEAR(read.players[0].wall_normal.x, 1.0f, 0.0001f);
  CHECK_NEAR(read.players[0].wall_contact_time, 0.08f, 0.0001f);
  CHECK_NEAR(read.players[0].wall_jump_cooldown, 0.2f, 0.0001f);
  CHECK_NEAR(read.players[0].dash_air_control_time, 0.15f, 0.0001f);
  CHECK(read.players[0].jump_held);
  CHECK(!read.players[0].dash_held);
  CHECK(read.players[0].air_jump_used);
  CHECK(read.players[0].slide_suppressed);

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
    std::snprintf(p.name, sizeof(p.name), "player_%08d", i);
    p.pos = {static_cast<float>(i), 1, 0};
    p.vel = {1, 2, 3};
    p.health = 100;
    p.frags = i;
    p.stamina = MAX_STAMINA;
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

TEST(snapshot_keeps_newest_events) {
  GameState s{};
  s.tick = 5;
  s.frag_limit = 20;
  s.next_event_id = 41;
  for (uint32_t id = 1; id <= 40; ++id) {
    GameEvent& e = s.events[id % MAX_EVENTS];
    e.id = id;
    e.type = EV_SOUND;
    e.a = SND_RIFLE;
    e.tick = 5;
  }

  uint8_t buf[MAX_PACKET];
  NetWriter w;
  nw_init(w, buf, sizeof(buf));
  snapshot_write(s, w);
  CHECK(!w.overflow);

  GameState read{};
  NetReader r;
  nr_init(r, buf, w.len);
  CHECK(snapshot_read(read, r));
  // Only 16 events fit per snapshot; they must be the 16 with the highest ids.
  CHECK_EQ_INT(read.events[40 % MAX_EVENTS].id, 40);
  CHECK_EQ_INT(read.events[25 % MAX_EVENTS].id, 25);
  CHECK_EQ_INT(read.events[24 % MAX_EVENTS].id, 0);
}

static Map prediction_test_map() {
  Map map{};
  map.boxes[map.box_count++] = {{-30, -1, -30}, {30, 0, 30}, {1, 1, 1}};
  map.boxes[map.box_count++] = {{6, 0, -4}, {7, 4, 4}, {1, 1, 1}};
  map.spawns[0] = {0, 0, 0};
  map.spawn_count = 1;
  return map;
}

// The contract client-side prediction relies on: serializing mid-run and
// replaying the remaining inputs on the deserialized state must land exactly
// where the server does. Fails if any field player_move() reads is missing
// from the snapshot.
TEST(prediction_replay_matches_server_sim) {
  Map map = prediction_test_map();
  Player server_p{};
  server_p.active = true;
  server_p.alive = true;
  server_p.player_class = CLASS_SCOUT;
  server_p.health = 85.0f;
  server_p.stamina = MAX_STAMINA;
  server_p.on_ground = true;

  PlayerInput inputs[24]{};
  for (int i = 0; i < 24; ++i) {
    PlayerInput& in = inputs[i];
    in.sequence = static_cast<uint32_t>(i + 1);
    in.buttons = BTN_FORWARD;
    if (i % 3 == 0) in.buttons |= BTN_JUMP;
    if (i == 4 || i == 16) in.buttons |= BTN_DASH;
    if (i >= 12 && i < 15) in.buttons |= BTN_CROUCH;
    if (i % 2 == 0) in.buttons |= BTN_RIGHT;
    in.yaw = 0.15f * static_cast<float>(i);
    in.pitch = 0.05f;
  }

  for (int i = 0; i < 12; ++i) {
    player_move(server_p, inputs[i], map, TICK_DT);
    server_p.last_input_seq = inputs[i].sequence;
  }
  GameState snap{};
  snap.tick = 12;
  snap.frag_limit = 20;
  snap.players[3] = server_p;

  uint8_t buf[MAX_PACKET];
  NetWriter w;
  nw_init(w, buf, sizeof(buf));
  snapshot_write(snap, w);
  CHECK(!w.overflow);
  GameState received{};
  NetReader r;
  nr_init(r, buf, w.len);
  CHECK(snapshot_read(received, r));

  Player client_p = received.players[3];
  for (int i = 12; i < 24; ++i) player_move(client_p, inputs[i], map, TICK_DT);
  for (int i = 12; i < 24; ++i) player_move(server_p, inputs[i], map, TICK_DT);

  CHECK_NEAR(client_p.pos.x, server_p.pos.x, 0.0005f);
  CHECK_NEAR(client_p.pos.y, server_p.pos.y, 0.0005f);
  CHECK_NEAR(client_p.pos.z, server_p.pos.z, 0.0005f);
  CHECK_NEAR(client_p.vel.x, server_p.vel.x, 0.0005f);
  CHECK_NEAR(client_p.vel.y, server_p.vel.y, 0.0005f);
  CHECK_NEAR(client_p.vel.z, server_p.vel.z, 0.0005f);
  CHECK_EQ_INT(client_p.stamina, server_p.stamina);
  CHECK(client_p.on_ground == server_p.on_ground);
  CHECK(client_p.sliding == server_p.sliding);
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

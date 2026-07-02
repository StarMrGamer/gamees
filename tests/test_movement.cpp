#include "test_harness.h"

#include "game/collision.h"
#include "game/map.h"
#include "game/movement.h"

#include <cmath>

static Map movement_map() {
  Map map{};
  map.boxes[map.box_count++] = {{-20, -1, -20}, {20, 0, 20}, {1, 1, 1}};
  map.spawns[0] = {0, 0, 0};
  map.spawn_count = 1;
  return map;
}

TEST(player_accelerates_jumps_and_dashes) {
  Map map = movement_map();
  Player p{};
  p.active = true;
  p.alive = true;
  p.health = PLAYER_MAX_HEALTH;
  p.pos = {0, 0, 0};
  p.on_ground = true;
  PlayerInput in{};
  in.buttons = BTN_FORWARD;
  in.yaw = 0.0f;
  for (int i = 0; i < 10; ++i) player_move(p, in, map, TICK_DT);
  CHECK(p.vel.z < -1.0f);

  in.buttons = BTN_JUMP;
  player_move(p, in, map, TICK_DT);
  CHECK(p.vel.y > 1.0f);

  in.buttons = BTN_DASH;
  float before = vec3_length({p.vel.x, 0, p.vel.z});
  player_move(p, in, map, TICK_DT);
  float after = vec3_length({p.vel.x, 0, p.vel.z});
  CHECK(after > before + 5.0f);
  CHECK(p.dash_cooldown > 1.0f);
}

TEST(jump_frame_preserves_bhop_momentum) {
  Map map = movement_map();
  Player p{};
  p.active = true;
  p.alive = true;
  p.health = PLAYER_MAX_HEALTH;
  p.pos = {0, 0, 0};
  p.vel = {12.0f, 0.0f, 0.0f};
  p.on_ground = true;

  PlayerInput in{};
  in.buttons = BTN_JUMP;
  float before = vec3_length({p.vel.x, 0, p.vel.z});
  player_move(p, in, map, TICK_DT);
  float after = vec3_length({p.vel.x, 0, p.vel.z});

  CHECK(!p.on_ground);
  CHECK(p.vel.y > 1.0f);
  CHECK(after > before * 0.98f);
}

TEST(air_strafe_uses_quake_style_wish_cap) {
  Map map = movement_map();
  Player p{};
  p.active = true;
  p.alive = true;
  p.health = PLAYER_MAX_HEALTH;
  p.pos = {0, 2, 0};
  p.vel = {0.0f, 0.0f, -8.0f};
  p.on_ground = false;

  PlayerInput in{};
  in.buttons = BTN_RIGHT;
  in.yaw = 0.0f;
  player_move(p, in, map, TICK_DT);

  CHECK(p.vel.x > 0.9f);
  CHECK(vec3_length({p.vel.x, 0.0f, p.vel.z}) > 8.05f);
}

TEST(arena_spawns_face_movable_space) {
  Map map{};
  CHECK(map_load("maps/arena.txt", &map));
  for (int i = 0; i < map.spawn_count; ++i) {
    CHECK(!map_box_overlap(map, player_aabb(map.spawns[i], false)));
    Player p{};
    p.active = true;
    p.alive = true;
    p.health = PLAYER_MAX_HEALTH;
    p.pos = map.spawns[i];
    p.on_ground = true;
    PlayerInput in{};
    in.buttons = BTN_FORWARD;
    in.yaw = map.spawn_yaws[i];
    Vec3 start = p.pos;
    float start_center_dist = vec3_length({start.x, 0.0f, start.z});
    for (int tick = 0; tick < TICK_RATE; ++tick) player_move(p, in, map, TICK_DT);
    float moved = vec3_length({p.pos.x - start.x, 0.0f, p.pos.z - start.z});
    float end_center_dist = vec3_length({p.pos.x, 0.0f, p.pos.z});
    CHECK(moved > 2.0f);
    CHECK(end_center_dist < start_center_dist - 1.0f);
  }
}

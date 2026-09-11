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

static Player movement_player(Vec3 pos, bool on_ground) {
  Player p{};
  p.active = true;
  p.alive = true;
  p.health = PLAYER_MAX_HEALTH;
  p.stamina = MAX_STAMINA;
  p.pos = pos;
  p.on_ground = on_ground;
  return p;
}

TEST(player_accelerates_jumps_and_dashes) {
  Map map = movement_map();
  Player p = movement_player({0, 0, 0}, true);
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
  CHECK(p.dash_cooldown > 0.1f);
  CHECK_EQ_INT(p.stamina, MAX_STAMINA - 1);
}

TEST(jump_frame_preserves_bhop_momentum) {
  Map map = movement_map();
  Player p = movement_player({0, 0, 0}, true);
  p.vel = {12.0f, 0.0f, 0.0f};

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
  Player p = movement_player({0, 2, 0}, false);
  p.vel = {0.0f, 0.0f, -8.0f};

  PlayerInput in{};
  in.buttons = BTN_RIGHT;
  in.yaw = 0.0f;
  player_move(p, in, map, TICK_DT);

  CHECK(p.vel.x > 0.9f);
  CHECK(vec3_length({p.vel.x, 0.0f, p.vel.z}) > 8.05f);
}

TEST(dash_jump_temporarily_improves_air_control) {
  Map map = movement_map();
  Player p = movement_player({0, 0, 0}, true);

  PlayerInput in{};
  in.buttons = BTN_DASH | BTN_JUMP | BTN_FORWARD;
  in.yaw = 0.0f;
  player_move(p, in, map, TICK_DT);
  CHECK(!p.on_ground);
  CHECK(p.dash_air_control_time > 0.20f);

  Player normal = movement_player({0, 2, 0}, false);
  normal.vel = {0.0f, 0.0f, -8.0f};
  Player boosted = normal;
  boosted.dash_air_control_time = DASH_JUMP_AIR_CONTROL_TIME;

  in.buttons = BTN_RIGHT;
  player_move(normal, in, map, TICK_DT);
  player_move(boosted, in, map, TICK_DT);
  CHECK(boosted.vel.x > normal.vel.x + 0.2f);
}

TEST(slide_jump_adds_momentum) {
  Map map = movement_map();
  Player p = movement_player({0, 0, 0}, true);
  p.vel = {0.0f, 0.0f, -10.0f};
  p.sliding = true;
  p.slide_time = SLIDE_DURATION;

  PlayerInput in{};
  in.buttons = BTN_JUMP | BTN_CROUCH;
  in.yaw = 0.0f;
  float before = vec3_length({p.vel.x, 0.0f, p.vel.z});
  player_move(p, in, map, TICK_DT);
  float after = vec3_length({p.vel.x, 0.0f, p.vel.z});
  CHECK(!p.on_ground);
  CHECK(after > before + SLIDE_JUMP_BOOST * 0.9f);
}

TEST(airborne_wall_jump_pushes_up_and_away) {
  Map map = movement_map();
  map.boxes[map.box_count++] = {{1.0f, 0.0f, -2.0f}, {1.8f, 5.0f, 2.0f}, {1, 1, 1}};
  Player p = movement_player({0.5f, 1.0f, 0.0f}, false);
  p.vel = {8.0f, 0.0f, 0.0f};

  PlayerInput in{};
  in.buttons = BTN_JUMP;
  in.yaw = 0.0f;
  player_move(p, in, map, TICK_DT);
  CHECK(p.vel.x < -WALL_JUMP_PUSH * 0.75f);
  CHECK(p.vel.y > WALL_JUMP_UP_VELOCITY * 0.9f);
}

TEST(stamina_gates_dash_and_recharges_on_ground) {
  Map map = movement_map();
  Player p = movement_player({0, 0, 0}, true);
  PlayerInput in{};
  in.buttons = BTN_DASH | BTN_FORWARD;
  in.yaw = 0.0f;
  player_move(p, in, map, TICK_DT);
  CHECK_EQ_INT(p.stamina, MAX_STAMINA - 1);

  player_move(p, in, map, TICK_DT);
  CHECK_EQ_INT(p.stamina, MAX_STAMINA - 1);

  in.buttons = 0;
  for (int i = 0; i < TICK_RATE; ++i) player_move(p, in, map, TICK_DT);
  in.buttons = BTN_DASH | BTN_FORWARD;
  player_move(p, in, map, TICK_DT);
  CHECK_EQ_INT(p.stamina, MAX_STAMINA - 2);

  p.stamina_recharge_timer = TICK_DT * 0.5f;
  in.buttons = 0;
  player_move(p, in, map, TICK_DT);
  CHECK_EQ_INT(p.stamina, MAX_STAMINA - 1);
}

TEST(player_classes_change_movement_profile) {
  Map map = movement_map();
  Player scout = movement_player({0, 0, 0}, true);
  Player tank = movement_player({0, 0, 0}, true);
  scout.player_class = CLASS_SCOUT;
  tank.player_class = CLASS_TANK;

  PlayerInput in{};
  in.buttons = BTN_FORWARD;
  in.yaw = 0.0f;
  for (int i = 0; i < TICK_RATE; ++i) {
    player_move(scout, in, map, TICK_DT);
    player_move(tank, in, map, TICK_DT);
  }

  float scout_speed = vec3_length({scout.vel.x, 0.0f, scout.vel.z});
  float tank_speed = vec3_length({tank.vel.x, 0.0f, tank.vel.z});
  CHECK(scout_speed > tank_speed + 1.0f);

  in.buttons = BTN_DASH | BTN_FORWARD;
  float scout_before = scout_speed;
  float tank_before = tank_speed;
  player_move(scout, in, map, TICK_DT);
  player_move(tank, in, map, TICK_DT);
  float scout_dash_gain = vec3_length({scout.vel.x, 0.0f, scout.vel.z}) - scout_before;
  float tank_dash_gain = vec3_length({tank.vel.x, 0.0f, tank.vel.z}) - tank_before;
  CHECK(scout_dash_gain > tank_dash_gain);
  CHECK(scout.dash_cooldown < tank.dash_cooldown);
}

TEST(double_jump_spends_stamina_once_per_airtime) {
  Map map = movement_map();
  Player p = movement_player({0, 2, 0}, false);
  p.vel = {0.0f, -2.0f, 0.0f};

  PlayerInput in{};
  in.buttons = BTN_JUMP | BTN_AIRJUMP;
  in.yaw = 0.0f;
  player_move(p, in, map, TICK_DT);
  CHECK(p.vel.y > DOUBLE_JUMP_VELOCITY * 0.9f);
  CHECK_EQ_INT(p.stamina, MAX_STAMINA - 1);
  CHECK(p.air_jump_used);

  in.buttons = 0;
  player_move(p, in, map, TICK_DT);
  in.buttons = BTN_JUMP | BTN_AIRJUMP;
  float before_y = p.vel.y;
  player_move(p, in, map, TICK_DT);
  CHECK(p.vel.y < before_y + 0.1f);
  CHECK_EQ_INT(p.stamina, MAX_STAMINA - 1);
}

TEST(double_jump_needs_its_own_button) {
  Map map = movement_map();

  // Jump button alone in mid-air (no wall) must NOT trigger the double jump.
  Player only_jump = movement_player({0, 2, 0}, false);
  only_jump.vel = {0.0f, -2.0f, 0.0f};
  PlayerInput in{};
  in.buttons = BTN_JUMP;
  in.yaw = 0.0f;
  player_move(only_jump, in, map, TICK_DT);
  CHECK(only_jump.vel.y < 0.0f);
  CHECK(!only_jump.air_jump_used);
  CHECK_EQ_INT(only_jump.stamina, MAX_STAMINA);

  // The dedicated airjump button triggers it.
  Player air = movement_player({0, 2, 0}, false);
  air.vel = {0.0f, -2.0f, 0.0f};
  in.buttons = BTN_AIRJUMP;
  player_move(air, in, map, TICK_DT);
  CHECK(air.vel.y > DOUBLE_JUMP_VELOCITY * 0.9f);
  CHECK(air.air_jump_used);
}

TEST(slide_boost_requires_rearm) {
  Map map = movement_map();
  Player p = movement_player({0, 0, 0}, true);
  p.vel = {12.0f, 0.0f, 0.0f};

  PlayerInput in{};
  in.buttons = BTN_CROUCH;
  player_move(p, in, map, TICK_DT);
  CHECK(p.sliding);
  CHECK_EQ_INT(p.move_sound, SND_SLIDE);
  CHECK(vec3_length({p.vel.x, 0.0f, p.vel.z}) > 12.5f);

  // Ride the slide out with crouch held; keep speed above the trigger so a
  // re-trigger would show up as a fresh +SLIDE_BOOST spike. Position is pinned
  // so the player can't slide off the test floor.
  for (int i = 0; i < TICK_RATE * 2; ++i) {
    p.pos = {0.0f, p.pos.y, 0.0f};
    p.vel = {12.0f, 0.0f, 0.0f};
    player_move(p, in, map, TICK_DT);
    CHECK(vec3_length({p.vel.x, 0.0f, p.vel.z}) < 13.0f);
  }
  CHECK(!p.sliding);
  CHECK(p.on_ground);
  p.pos = {0.0f, p.pos.y, 0.0f};

  // Releasing crouch re-arms the slide.
  in.buttons = 0;
  player_move(p, in, map, TICK_DT);
  p.vel = {12.0f, 0.0f, 0.0f};
  in.buttons = BTN_CROUCH;
  player_move(p, in, map, TICK_DT);
  CHECK(p.sliding);
}

TEST(player_move_reports_movement_sounds) {
  Map map = movement_map();
  Player p = movement_player({0, 0, 0}, true);

  PlayerInput in{};
  in.buttons = BTN_JUMP;
  player_move(p, in, map, TICK_DT);
  CHECK_EQ_INT(p.move_sound, SND_JUMP);

  in.buttons = 0;
  player_move(p, in, map, TICK_DT);
  CHECK_EQ_INT(p.move_sound, 0);

  Player faller = movement_player({0, 3.0f, 0}, false);
  faller.vel = {0.0f, -6.0f, 0.0f};
  int guard = 0;
  while (!faller.on_ground && guard++ < TICK_RATE * 3) {
    player_move(faller, in, map, TICK_DT);
  }
  CHECK(faller.on_ground);
  CHECK_EQ_INT(faller.move_sound, SND_LAND);

  Player dasher = movement_player({0, 0, 0}, true);
  PlayerInput dash_in{};
  dash_in.buttons = BTN_DASH | BTN_FORWARD;
  player_move(dasher, dash_in, map, TICK_DT);
  CHECK_EQ_INT(dasher.move_sound, SND_DASH);
}

TEST(arena_spawns_face_movable_space) {
  Map map{};
  CHECK(map_load("maps/arena.txt", &map));
  for (int i = 0; i < map.spawn_count; ++i) {
    CHECK(!map_box_overlap(map, player_aabb(map.spawns[i], false)));
    Player p = movement_player(map.spawns[i], true);
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

TEST(player_walks_up_a_ramp) {
  Map map{};
  // Slope rising from y=0 at x=-10 to y=5 at x=10, i.e. the plane through
  // (-10, 0) and (10, 5): 5x - 20y = -50, normalised.
  {
    Vec3 n = vec3_normalize({-5.0f, 20.0f, 0.0f});
    map.ramps[map.ramp_count++] = {{-10.0f, 0.0f, -5.0f}, {10.0f, 5.0f, 5.0f},
                                   {0.5f, 0.5f, 0.5f}, n,
                                   vec3_dot(n, Vec3{-10.0f, 0.0f, 0.0f})};
  }
  map.spawns[0] = {-9.0f, 0.0f, 0.0f};
  map.spawn_count = 1;

  Player p = movement_player({-9.0f, 0.0f, 0.0f}, true);
  PlayerInput in{};
  in.buttons = BTN_FORWARD;
  in.yaw = PI * 0.5f;  // face +x, up the ramp
  for (int tick = 0; tick < 30; ++tick) player_move(p, in, map, TICK_DT);

  CHECK(p.pos.y > 0.5f);   // climbed the slope
  CHECK(p.pos.x > -6.0f);  // made forward progress
  CHECK(p.on_ground);
}

TEST(player_falling_into_the_void_is_teleported_to_spawn) {
  Map map{};
  map.void_y = -10.0f;
  map.spawns[0] = {3.0f, 0.0f, 4.0f};
  map.spawn_count = 1;

  Player p = movement_player({0.0f, -20.0f, 0.0f}, false);
  p.vel = {5.0f, -10.0f, 2.0f};
  PlayerInput in{};
  player_move(p, in, map, TICK_DT);

  CHECK_NEAR(p.pos.x, 3.0f, 0.001f);
  CHECK_NEAR(p.pos.y, 0.0f, 0.001f);
  CHECK_NEAR(p.pos.z, 4.0f, 0.001f);
  CHECK_NEAR(p.vel.x, 0.0f, 0.001f);
  CHECK_NEAR(p.vel.y, 0.0f, 0.001f);
  CHECK_NEAR(p.vel.z, 0.0f, 0.001f);
}

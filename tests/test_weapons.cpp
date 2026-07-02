#include "test_harness.h"

#include "game/sim.h"
#include "game/tuning.h"
#include "game/weapons.h"

static Map weapons_map() {
  Map map{};
  map.boxes[map.box_count++] = {{-20, -1, -20}, {20, 0, 20}, {1, 1, 1}};
  map.spawns[0] = {0, 0, 0};
  map.spawns[1] = {0, 0, -5};
  map.spawn_count = 2;
  return map;
}

TEST(rifle_hits_target) {
  Map map = weapons_map();
  GameState s{};
  game_init(s, map, 20);
  int a = game_player_join(s, map, "a");
  int b = game_player_join(s, map, "b");
  s.players[a].pos = {0, 0, 0};
  s.players[a].yaw = 0.0f;
  s.players[a].pitch = 0.0f;
  s.players[b].pos = {0, 0, -5};
  s.players[b].health = 100.0f;
  weapon_fire(s, map, a);
  CHECK_NEAR(s.players[b].health, 100.0f - RIFLE_DAMAGE, 0.0001f);
}

TEST(rifle_hits_far_target_immediately) {
  Map map = weapons_map();
  GameState s{};
  game_init(s, map, 20);
  int a = game_player_join(s, map, "a");
  int b = game_player_join(s, map, "b");
  s.players[a].pos = {0, 0, 0};
  s.players[a].yaw = 0.0f;
  s.players[a].pitch = 0.0f;
  s.players[b].pos = {0, 0, -80};
  s.players[b].health = 100.0f;
  weapon_fire(s, map, a);
  CHECK_NEAR(s.players[b].health, 100.0f - RIFLE_DAMAGE, 0.0001f);
  CHECK_EQ_INT(s.rockets[0].active ? 1 : 0, 0);
}

TEST(rocket_spawns_from_side_muzzle) {
  Map map = weapons_map();
  GameState s{};
  game_init(s, map, 20);
  int a = game_player_join(s, map, "a");
  s.players[a].pos = {0, 0, 0};
  s.players[a].yaw = 0.0f;
  s.players[a].pitch = 0.0f;
  s.players[a].weapon = WEAPON_ROCKET;
  weapon_fire(s, map, a);
  CHECK(s.rockets[0].active);
  CHECK(s.rockets[0].pos.x > WEAPON_MUZZLE_RIGHT * 0.8f);
  CHECK(s.rockets[0].pos.z < -WEAPON_MUZZLE_FORWARD * 0.8f);
  CHECK(s.rockets[0].vel.z < -ROCKET_SPEED * 0.95f);
}

TEST(scout_primary_is_close_range_shotgun) {
  Map map = weapons_map();
  GameState s{};
  game_init(s, map, 20);
  int a = game_player_join(s, map, "a");
  int b = game_player_join(s, map, "b");

  game_set_player_class(s.players[a], CLASS_SCOUT);
  s.players[a].pos = {0, 0, 0};
  s.players[a].yaw = 0.0f;
  s.players[a].pitch = 0.0f;
  s.players[b].pos = {0, 0, -2};
  s.players[b].health = 100.0f;
  weapon_fire(s, map, a);

  CHECK_EQ_INT(s.players[a].weapon, WEAPON_SHOTGUN);
  CHECK(s.players[b].health < 100.0f - RIFLE_DAMAGE);
  CHECK_NEAR(s.players[a].fire_cooldown, SHOTGUN_INTERVAL, 0.0001f);
}

TEST(tank_primary_is_fast_low_damage_lmg) {
  Map map = weapons_map();
  GameState s{};
  game_init(s, map, 20);
  int a = game_player_join(s, map, "a");
  int b = game_player_join(s, map, "b");

  game_set_player_class(s.players[a], CLASS_TANK);
  s.players[a].pos = {0, 0, 0};
  s.players[a].yaw = 0.0f;
  s.players[a].pitch = 0.0f;
  s.players[b].pos = {0, 0, -5};
  s.players[b].health = 100.0f;
  weapon_fire(s, map, a);

  CHECK_EQ_INT(s.players[a].weapon, WEAPON_LMG);
  CHECK(s.players[b].health > 100.0f - RIFLE_DAMAGE);
  CHECK(s.players[b].health < 100.0f);
  CHECK(s.players[a].fire_cooldown < RIFLE_INTERVAL);
}

TEST(rocket_point_blank_hit_does_not_one_shot) {
  Map map = weapons_map();
  GameState s{};
  game_init(s, map, 20);
  int a = game_player_join(s, map, "a");
  int b = game_player_join(s, map, "b");
  s.players[a].pos = {0, 0, 0};
  s.players[b].pos = {0, 0, -0.5f};
  s.rockets[0].active = true;
  s.rockets[0].owner = static_cast<uint8_t>(a);
  s.rockets[0].pos = {0, 0.9f, -0.5f};
  explode_rocket(s, map, 0);
  CHECK(s.players[b].alive);
  CHECK_NEAR(s.players[b].health, PLAYER_MAX_HEALTH - ROCKET_DIRECT_DAMAGE, 0.0001f);
  CHECK_EQ_INT(s.players[a].frags, 0);
}

TEST(rocket_jump_launches_owner_with_reduced_self_damage) {
  Map map = weapons_map();
  GameState s{};
  game_init(s, map, 20);
  int a = game_player_join(s, map, "a");
  s.players[a].pos = {0, 0, 0};
  s.players[a].health = PLAYER_MAX_HEALTH;
  s.rockets[0].active = true;
  s.rockets[0].owner = static_cast<uint8_t>(a);
  s.rockets[0].pos = {0, 0.15f, 0};
  explode_rocket(s, map, 0);
  CHECK(s.players[a].alive);
  CHECK(s.players[a].vel.y > ROCKET_JUMP_MIN_UP);
  CHECK(s.players[a].health > PLAYER_MAX_HEALTH - ROCKET_DIRECT_DAMAGE);
}

TEST(rocket_splash_kills_and_scores_wounded_target) {
  Map map = weapons_map();
  GameState s{};
  game_init(s, map, 20);
  int a = game_player_join(s, map, "a");
  int b = game_player_join(s, map, "b");
  s.players[a].pos = {0, 0, 0};
  s.players[b].pos = {0, 0, -0.5f};
  s.players[b].health = 60.0f;
  s.rockets[0].active = true;
  s.rockets[0].owner = static_cast<uint8_t>(a);
  s.rockets[0].pos = {0, 0.9f, -0.5f};
  explode_rocket(s, map, 0);
  CHECK(!s.players[b].alive);
  CHECK_EQ_INT(s.players[a].frags, 1);
}

TEST(class_switch_preserves_health_fraction_and_scales_damage) {
  Map map = weapons_map();
  GameState s{};
  game_init(s, map, 20);
  int a = game_player_join(s, map, "a");
  int b = game_player_join(s, map, "b");
  s.players[a].health = 50.0f;

  PlayerInput inputs[MAX_PLAYERS]{};
  inputs[a].class_switch = 3;
  Rng rng{1234};
  game_tick(s, map, inputs, rng);
  CHECK_EQ_INT(s.players[a].player_class, CLASS_TANK);
  CHECK_EQ_INT(s.players[a].weapon, WEAPON_LMG);
  CHECK_NEAR(s.players[a].health, player_class_max_health(CLASS_TANK) * 0.5f, 0.01f);

  s.players[b].pos = {0, 0, -5};
  s.players[b].health = player_class_max_health(CLASS_TANK);
  s.players[b].player_class = CLASS_TANK;
  damage_player(s, b, a, 50.0f, {0, 0, 0});
  CHECK_NEAR(s.players[b].health,
             player_class_max_health(CLASS_TANK) - 50.0f * player_class_damage_taken_scale(CLASS_TANK),
             0.001f);
}

#include "test_harness.h"

#include "game/sim.h"
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

TEST(rocket_splash_kills_and_scores) {
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
  CHECK(!s.players[b].alive);
  CHECK_EQ_INT(s.players[a].frags, 1);
}

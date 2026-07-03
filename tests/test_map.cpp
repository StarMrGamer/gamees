#include "test_harness.h"

#include "game/collision.h"
#include "game/map.h"

TEST(map_parse_basic) {
  const char* text =
    "name test\n"
    "box -1 -1 -1 2 1 2 0.1 0.2 0.3\n"
    "spawn 0 0 0 90\n"
    "health 1 0 1\n"
    "light 0 -1 0\n"
    "fog 0.5 0.6 0.7 0.01\n"
    "sky 0.2 0.3 0.4\n";
  Map map{};
  CHECK(map_parse(text, &map));
  CHECK_EQ_INT(map.box_count, 1);
  CHECK_EQ_INT(map.spawn_count, 1);
  CHECK_EQ_INT(map.health_count, 1);
  CHECK_NEAR(map.spawn_yaws[0], PI * 0.5f, 0.0001f);
  CHECK_NEAR(map.boxes[0].max.x, 1.0f, 0.0001f);
}

TEST(shipped_arena_map_is_valid) {
  Map map{};
  CHECK(map_load("maps/arena.txt", &map));
  CHECK_EQ_INT(map.spawn_count, 8);
  CHECK(map.box_count > 20);
  CHECK(map.box_count < MAX_MAP_BOXES);
  CHECK(map.health_count >= 3);

  // No spawn is embedded in solid geometry (standing hull is clear).
  for (int i = 0; i < map.spawn_count; ++i) {
    CHECK(!map_box_overlap(map, player_aabb(map.spawns[i], false)));
  }

  // Every health pack rests just above a walkable surface: not buried in a
  // wall, not floating out of reach.
  for (int i = 0; i < map.health_count; ++i) {
    Vec3 h = map.health_spawns[i];
    Aabb probe{{h.x - 0.15f, h.y - 0.15f, h.z - 0.15f},
               {h.x + 0.15f, h.y + 0.15f, h.z + 0.15f}};
    CHECK(!map_box_overlap(map, probe));
    bool supported = false;
    for (int b = 0; b < map.box_count && !supported; ++b) {
      const MapBox& box = map.boxes[b];
      float top = box.max.y;
      supported = top <= h.y - 0.2f && top >= h.y - 1.1f &&
                  h.x >= box.min.x && h.x <= box.max.x &&
                  h.z >= box.min.z && h.z <= box.max.z;
    }
    CHECK(supported);
  }
}

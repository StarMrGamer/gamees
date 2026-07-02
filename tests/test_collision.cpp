#include "test_harness.h"

#include "game/collision.h"

static Map collision_map() {
  Map map{};
  map.boxes[map.box_count++] = {{-20, -1, -20}, {20, 0, 20}, {1, 1, 1}};
  map.boxes[map.box_count++] = {{2, 0, -1}, {3, 3, 1}, {1, 1, 1}};
  map.spawns[0] = {0, 0, 0};
  map.spawn_count = 1;
  return map;
}

TEST(aabb_and_ray) {
  Aabb a{{0, 0, 0}, {1, 1, 1}};
  Aabb b{{0.5f, 0.5f, 0.5f}, {2, 2, 2}};
  CHECK(aabb_overlap(a, b));
  float t = 0.0f;
  CHECK(ray_aabb({-2, 0.5f, 0.5f}, {1, 0, 0}, a, 10.0f, &t));
  CHECK_NEAR(t, 2.0f, 0.0001f);
}

TEST(move_slide_floor_and_wall) {
  Map map = collision_map();
  MoveResult fall = move_slide(map, {0, 1, 0}, {0, -10, 0}, false, 0.2f);
  CHECK(fall.on_ground);
  CHECK_NEAR(fall.pos.y, 0.0f, 0.0001f);

  MoveResult wall = move_slide(map, {1.5f, 0, 0}, {10, 0, 0}, false, 0.2f);
  CHECK_NEAR(wall.vel.x, 0.0f, 0.0001f);
}

TEST(step_up_requires_ground_contact) {
  Map map{};
  map.boxes[map.box_count++] = {{-20, -1, -20}, {20, 0, 20}, {1, 1, 1}};
  map.boxes[map.box_count++] = {{0.6f, 0.0f, -0.5f}, {1.2f, 0.25f, 0.5f}, {1, 1, 1}};

  MoveResult grounded = move_slide(map, {0, 0, 0}, {8, 0, 0}, false, 0.1f);
  CHECK(grounded.on_ground);
  CHECK(grounded.pos.y > 0.20f);

  MoveResult airborne = move_slide(map, {0, 0.2f, 0}, {8, 0, 0}, false, 0.1f);
  CHECK_NEAR(airborne.vel.x, 0.0f, 0.0001f);
  CHECK(airborne.pos.y < 0.25f);
}

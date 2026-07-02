#include "test_harness.h"

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

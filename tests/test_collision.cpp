#include "test_harness.h"

#include "core/rng.h"
#include "game/collision.h"
#include "game/tuning.h"

#include <memory>
#include <string>

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

// Regression: move_slide's ramp snap used to write the slope height into the
// player's Y without checking it. Imported maps routinely have a ramp running
// under a low overhang, so the snap lifted the player's head inside that
// brush - and once inside, blocked_at() rejects every direction and the player
// is wedged permanently.
//
// The layout below is the minimal reproduction. PLAYER_HEIGHT is 1.8 and
// STEP_HEIGHT is 0.4, so an overhang whose underside sits at y=2.0 leaves a
// standing player at y=0 exactly 0.2 of clearance: legal where they stand,
// but inside the overhang once a 0.3 ramp snap lifts them.
static void build_overhang_ramp_map(Map* map) {
  std::string text =
      "name snaptest\n"
      "box -20 -1 -20 40 1 40 0.5 0.5 0.5\n"
      "ramp 0 0 -6 12 3 12 +x 0.3 0.6 0.3\n"
      "box 0 2 -6 12 2 12 0.6 0.3 0.3\n"
      "spawn -4 0 0 0\n";
  CHECK(map_parse(text.c_str(), map));
}

TEST(move_slide_ramp_snap_does_not_push_player_into_an_overhang) {
  static Map map;
  build_overhang_ramp_map(&map);

  // x = 1.2 puts the ramp surface at 3 * (1.2 / 12) = 0.3, a legal step up.
  Vec3 pos{1.2f, 0.0f, 0.0f};
  CHECK(!map_box_overlap(map, player_aabb(pos, false)));

  float surface = 0.0f;
  CHECK(map_ramp_surface(map, pos.x, pos.z, &surface));
  CHECK_NEAR(surface, 0.3f, 0.01f);
  // The snap target really is inside the overhang - otherwise this test would
  // pass for the wrong reason.
  CHECK(map_box_overlap(map, player_aabb({pos.x, surface, pos.z}, false)));

  MoveResult r = move_slide(map, pos, {0.0f, -1.0f, 0.0f}, false, TICK_DT);
  CHECK(!map_box_overlap(map, player_aabb(r.pos, false)));
}

// The general invariant: move_slide must never return a position whose hull is
// inside solid geometry, from any legal starting position.
TEST(move_slide_never_ends_inside_solid_geometry) {
  static Map map;
  build_overhang_ramp_map(&map);

  Rng rng{0x5A5A};
  int tested = 0;
  int would_have_wedged = 0;
  for (int i = 0; i < 6000; ++i) {
    Vec3 pos{rng_float(rng, -4.0f, 14.0f), rng_float(rng, 0.0f, 3.0f),
             rng_float(rng, -8.0f, 8.0f)};
    bool crouching = (i & 1) != 0;
    // Only start from a legal position; the function makes no promises about
    // recovering from one that is already inside a wall.
    if (map_box_overlap(map, player_aabb(pos, crouching))) continue;
    ++tested;

    float surface = 0.0f;
    if (map_ramp_surface(map, pos.x, pos.z, &surface) &&
        map_box_overlap(map, player_aabb({pos.x, surface, pos.z}, crouching))) {
      ++would_have_wedged;
    }

    Vec3 vel{rng_float(rng, -12.0f, 12.0f), rng_float(rng, -9.0f, 6.0f),
             rng_float(rng, -12.0f, 12.0f)};
    MoveResult r = move_slide(map, pos, vel, crouching, TICK_DT);
    CHECK(!map_box_overlap(map, player_aabb(r.pos, crouching)));
  }
  CHECK(tested > 1000);
  // Confirms the sample set actually contains the dangerous case.
  CHECK(would_have_wedged > 50);
}

// Walking up a clean ramp must still work - the fix above must not have
// disabled ramp snapping wherever there is no conflicting solid.
TEST(move_slide_still_climbs_a_clear_ramp) {
  static Map map;
  std::string text =
      "name rampclimb\n"
      "box -20 -1 -20 40 1 40 0.5 0.5 0.5\n"
      "ramp 0 0 -4 10 4 8 +x 0.3 0.6 0.3\n"
      "spawn -2 1 0 0\n";
  CHECK(map_parse(text.c_str(), &map));

  Vec3 pos{-1.0f, 0.0f, 0.0f};
  float start_y = pos.y;
  for (int i = 0; i < 120; ++i) {
    MoveResult r = move_slide(map, pos, {6.0f, -1.0f, 0.0f}, false, TICK_DT);
    pos = r.pos;
  }
  // Two seconds of walking +x should have carried the player well up the slope
  // rather than leaving them at the bottom.
  CHECK(pos.x > 1.0f);
  CHECK(pos.y > start_y + 0.5f);
  CHECK(!map_box_overlap(map, player_aabb(pos, false)));
}

// Regression: a ramp used to block the player whenever its *surface* was above
// them, ignoring where the ramp's solid body actually was. Terrain stacks - a
// hillside arching overhead would mark the whole column under it as rock, so
// ground-level areas (including ones with spawns in them) were unenterable.
// Reported from play at -22.76 3.28 20.94 on de_dust2.
TEST(ramp_overhead_does_not_block_the_ground_below_it) {
  static Map map;
  std::string text =
      "name stacked\n"
      // Ground terrain at y = 0..0.5, and a ledge 6 m up over the same ground.
      "ramp -10 0 -10 20 0.5 20 0 1 0 0.5 0.5 0.5 0.5\n"
      "ramp -10 6 -10 20 0.5 20 0 1 0 6.5 0.4 0.4 0.4\n"
      "spawn 0 0.5 0 0\n";
  CHECK(map_parse(text.c_str(), &map));
  CHECK_EQ_INT(map.ramp_count, 2);

  // Standing on the lower surface, the ledge overhead must not count as solid.
  Vec3 stand{0.0f, 0.5f, 0.0f};
  CHECK(!map_ramp_blocks(map, stand, false));

  // And the ground underfoot must still be found, not masked by the ledge.
  float surf = 0.0f;
  CHECK(map_ramp_surface_near(map, stand.x, stand.z, stand.y, &surf));
  CHECK_NEAR(surf, 0.5f, 0.05f);

  // The plain query returns the highest surface in the column - which is why
  // the grounding path must not use it.
  CHECK(map_ramp_surface(map, stand.x, stand.z, &surf));
  CHECK_NEAR(surf, 6.5f, 0.05f);

  // Walking around at ground level stays unobstructed.
  Rng rng{0x5AB};
  for (int i = 0; i < 500; ++i) {
    Vec3 p{rng_float(rng, -9.0f, 9.0f), 0.5f, rng_float(rng, -9.0f, 9.0f)};
    CHECK(!map_ramp_blocks(map, p, false));
  }

  // Directly inside the upper slab is still solid.
  CHECK(map_ramp_blocks(map, {0.0f, 5.6f, 0.0f}, false));
}

// Walking into the face of a ramp that really is in the way must still be
// blocked - the fix above must not have made ramps passable.
TEST(ramp_face_still_blocks_when_it_overlaps_the_player) {
  static Map map;
  std::string text =
      "name wall\n"
      "box -20 -1 -20 40 1 40 0.5 0.5 0.5\n"
      // A slope climbing steeply from y=0 at x=0 to y=6 at x=6.
      "ramp 0 0 -5 6 6 10 -6 6 0 0 0.5 0.5 0.5\n"
      "spawn -5 0 0 0\n";
  CHECK(map_parse(text.c_str(), &map));

  // At the foot of the slope the surface is at the feet: passable.
  CHECK(!map_ramp_blocks(map, {0.2f, 0.0f, 0.0f}, false));
  // Further up, the surface is well overhead and the body is inside the wedge.
  CHECK(map_ramp_blocks(map, {5.0f, 0.0f, 0.0f}, false));
}

// Terrain stacks: an imported hillside often arches over ground the player is
// standing on. The ground test has to look at the surface underfoot, not the
// highest surface in the column - de_dust2 has 9027 standing positions where
// the two answers differ, and every one of them is a spot a reporting tool
// would describe wrongly if it rolled its own rule.
TEST(grounded_at_ignores_ramps_overhead) {
  auto map = std::make_unique<Map>();
  CHECK(map_parse("name stacked\n"
                  "ramp -5 0 -5 10 1 10 +x 0.5 0.5 0.5\n"   // ground underfoot
                  "ramp -5 9 -5 10 1 10 +x 0.5 0.5 0.5\n"   // hillside overhead
                  "spawn 0 2 0 0\n",
                  map.get()));
  CHECK_EQ_INT(map->ramp_count, 2);

  // Both ramps rise 1 m over their 10 m run, so at x = 0 the lower surface is
  // at 0.5 and the one overhead at 9.5.
  const float foot_y = 0.5f;
  const float over_y = 9.5f;
  float highest = 0.0f;
  CHECK(map_ramp_surface(*map, 0.0f, 0.0f, &highest));
  CHECK_NEAR(highest, over_y, 0.001f);  // the unbounded query sees the hillside

  // Standing on the lower ramp. The old rule compared the feet against
  // `highest` and concluded the player was in mid-air 9 m below a surface.
  CHECK(map_grounded_at(*map, {0.0f, foot_y, 0.0f}, false));
  // Standing on the hillside itself is grounded too.
  CHECK(map_grounded_at(*map, {0.0f, over_y, 0.0f}, false));
  // Genuinely between the two is not.
  CHECK(!map_grounded_at(*map, {0.0f, foot_y + 4.0f, 0.0f}, false));
}

#include "core/rng.h"
#include "game/collision.h"
#include "game/map.h"
#include "test_harness.h"

#include <cmath>
#include <memory>
#include <string>

namespace {

// A map with enough geometry to make the grid do real pruning: a floor, a ring
// of walls, scattered pillars at varying heights, and ramps facing all four
// directions.
std::string busy_map_text() {
  std::string t = "name gridtest\n";
  t += "box -40 -1 -40 80 1 80 0.5 0.5 0.5\n";
  t += "box -40 0 -40 80 8 1 0.4 0.4 0.4\n";
  t += "box -40 0 39 80 8 1 0.4 0.4 0.4\n";
  t += "box -40 0 -40 1 8 80 0.4 0.4 0.4\n";
  t += "box 39 0 -40 1 8 80 0.4 0.4 0.4\n";
  Rng rng{0xABCDEF};
  for (int i = 0; i < 220; ++i) {
    float x = rng_float(rng, -36.0f, 32.0f);
    float z = rng_float(rng, -36.0f, 32.0f);
    float w = rng_float(rng, 0.8f, 4.5f);
    float d = rng_float(rng, 0.8f, 4.5f);
    float h = rng_float(rng, 0.6f, 6.0f);
    char line[160];
    std::snprintf(line, sizeof(line), "box %.2f 0 %.2f %.2f %.2f %.2f 0.6 0.3 0.3\n",
                  x, z, w, h, d);
    t += line;
  }
  const char* dirs[4] = {"+x", "-x", "+z", "-z"};
  for (int i = 0; i < 40; ++i) {
    float x = rng_float(rng, -34.0f, 28.0f);
    float z = rng_float(rng, -34.0f, 28.0f);
    char line[160];
    std::snprintf(line, sizeof(line), "ramp %.2f 0 %.2f 5 2.5 5 %s 0.3 0.6 0.3\n",
                  x, z, dirs[i % 4]);
    t += line;
  }
  t += "spawn 0 1 0 0\n";
  t += "spawn 10 1 10 90\n";
  return t;
}

}  // namespace

// The spatial index is a pure optimisation: for every query it must return
// exactly what the brute-force scan returns. This drives both paths over the
// same random samples and compares them.
TEST(map_grid_matches_brute_force) {
  auto indexed = std::make_unique<Map>();
  std::string text = busy_map_text();
  CHECK(map_parse(text.c_str(), indexed.get()));
  CHECK(indexed->grid.built);
  CHECK(indexed->box_count > 200);
  CHECK(indexed->ramp_count == 40);

  // The same map with the index switched off exercises the fallback path.
  auto brute = std::make_unique<Map>(*indexed);
  brute->grid.built = false;

  Rng rng{0x1234};
  int overlap_hits = 0;
  int ramp_hits = 0;
  int ray_hits = 0;
  for (int i = 0; i < 4000; ++i) {
    Vec3 p{rng_float(rng, -45.0f, 45.0f), rng_float(rng, -3.0f, 12.0f),
           rng_float(rng, -45.0f, 45.0f)};

    Aabb box = player_aabb(p, (i & 1) != 0);
    bool a = map_box_overlap(*indexed, box);
    bool b = map_box_overlap(*brute, box);
    CHECK(a == b);
    if (a) ++overlap_hits;

    float ya = 0.0f;
    float yb = 0.0f;
    bool ra = map_ramp_surface(*indexed, p.x, p.z, &ya);
    bool rb = map_ramp_surface(*brute, p.x, p.z, &yb);
    CHECK(ra == rb);
    if (ra) {
      CHECK_NEAR(ya, yb, 0.0001f);
      ++ramp_hits;
    }

    Vec3 dir = angles_forward(rng_float(rng, -PI, PI), rng_float(rng, -1.4f, 1.4f));
    float ta = ray_map(*indexed, p, dir, 150.0f);
    float tb = ray_map(*brute, p, dir, 150.0f);
    CHECK_NEAR(ta, tb, 0.001f);
    if (tb < 150.0f) ++ray_hits;
  }

  // Guard against a vacuous pass where the samples never touched geometry.
  CHECK(overlap_hits > 50);
  CHECK(ramp_hits > 50);
  CHECK(ray_hits > 500);
}

// Axis-aligned rays are the DDA's degenerate cases: one of the two step
// directions is zero, so the walk must not stall or skip the first cell.
TEST(map_grid_axis_aligned_rays) {
  auto indexed = std::make_unique<Map>();
  std::string text = busy_map_text();
  CHECK(map_parse(text.c_str(), indexed.get()));
  auto brute = std::make_unique<Map>(*indexed);
  brute->grid.built = false;

  const Vec3 dirs[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}, {0, 1, 0}, {0, -1, 0}};
  Rng rng{0x99};
  for (int i = 0; i < 400; ++i) {
    Vec3 p{rng_float(rng, -38.0f, 38.0f), rng_float(rng, 0.2f, 7.0f),
           rng_float(rng, -38.0f, 38.0f)};
    for (const Vec3& d : dirs) {
      CHECK_NEAR(ray_map(*indexed, p, d, 150.0f), ray_map(*brute, p, d, 150.0f), 0.001f);
    }
  }
}

// A ray that starts well outside the indexed footprint still has to find the
// geometry it flies into.
TEST(map_grid_rays_from_outside) {
  auto indexed = std::make_unique<Map>();
  std::string text = busy_map_text();
  CHECK(map_parse(text.c_str(), indexed.get()));
  auto brute = std::make_unique<Map>(*indexed);
  brute->grid.built = false;

  Rng rng{0x777};
  int hits = 0;
  for (int i = 0; i < 800; ++i) {
    Vec3 p{rng_float(rng, -300.0f, -120.0f), rng_float(rng, -20.0f, 30.0f),
           rng_float(rng, -300.0f, 300.0f)};
    Vec3 target{rng_float(rng, -30.0f, 30.0f), rng_float(rng, 0.0f, 6.0f),
                rng_float(rng, -30.0f, 30.0f)};
    Vec3 dir = vec3_normalize(target - p);
    float ta = ray_map(*indexed, p, dir, 600.0f);
    float tb = ray_map(*brute, p, dir, 600.0f);
    CHECK_NEAR(ta, tb, 0.01f);
    if (tb < 600.0f) ++hits;
  }
  CHECK(hits > 200);
}

// A map with no geometry at all must not build a degenerate grid.
TEST(map_grid_empty_geometry) {
  auto m = std::make_unique<Map>();
  // Parsing succeeds only with a spawn; geometry is optional.
  CHECK(map_parse("name empty\nspawn 0 1 0 0\n", m.get()));
  CHECK(!m->grid.built);
  CHECK(!map_box_overlap(*m, player_aabb({0.0f, 0.0f, 0.0f}, false)));
  CHECK_NEAR(ray_map(*m, {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, 50.0f), 50.0f, 0.001f);
}

namespace {

// A tall, multi-storey map: every XZ column holds several floors far apart in
// height. This is what the grid's per-entry Y band exists for - the index is
// 2D, so without the band a query at ground level still has to look at the
// roof - and it is where an over-eager band cull would start losing geometry.
std::string tall_map_text() {
  std::string t = "name tallgrid\n";
  Rng rng{0x5150};
  for (int floor = 0; floor < 9; ++floor) {
    float y = static_cast<float>(floor) * 6.0f - 12.0f;
    char slab[160];
    // A full storey slab, so every cell in the grid has geometry at this height.
    std::snprintf(slab, sizeof(slab), "box -30 %.2f -30 60 0.5 60 0.5 0.5 0.55\n", y);
    t += slab;
    for (int i = 0; i < 24; ++i) {
      char line[160];
      std::snprintf(line, sizeof(line), "box %.2f %.2f %.2f %.2f %.2f %.2f 0.6 0.3 0.3\n",
                    rng_float(rng, -28.0f, 22.0f), y + 0.5f, rng_float(rng, -28.0f, 22.0f),
                    rng_float(rng, 1.0f, 5.0f), rng_float(rng, 0.8f, 4.0f),
                    rng_float(rng, 1.0f, 5.0f));
      t += line;
    }
    const char* dirs[4] = {"+x", "-x", "+z", "-z"};
    for (int i = 0; i < 6; ++i) {
      char line[160];
      std::snprintf(line, sizeof(line), "ramp %.2f %.2f %.2f 5 2.5 5 %s 0.3 0.6 0.3\n",
                    rng_float(rng, -26.0f, 20.0f), y + 0.5f, rng_float(rng, -26.0f, 20.0f),
                    dirs[i % 4]);
      t += line;
    }
  }
  t += "spawn 0 1 0 0\n";
  return t;
}

}  // namespace

// Same contract as map_grid_matches_brute_force, on geometry stacked over 54 m
// of height so the Y band is doing real work rather than passing everything.
TEST(map_grid_tall_map_matches_brute_force) {
  auto indexed = std::make_unique<Map>();
  std::string text = tall_map_text();
  CHECK(map_parse(text.c_str(), indexed.get()));
  CHECK(indexed->grid.built);
  CHECK(indexed->box_count > 200);
  CHECK(indexed->ramp_count == 54);

  auto brute = std::make_unique<Map>(*indexed);
  brute->grid.built = false;

  Rng rng{0xBEEF};
  int overlap_hits = 0;
  int ramp_hits = 0;
  int near_hits = 0;
  int ray_hits = 0;
  for (int i = 0; i < 6000; ++i) {
    Vec3 p{rng_float(rng, -32.0f, 32.0f), rng_float(rng, -14.0f, 44.0f),
           rng_float(rng, -32.0f, 32.0f)};

    Aabb box = player_aabb(p, (i & 1) != 0);
    bool a = map_box_overlap(*indexed, box);
    CHECK(a == map_box_overlap(*brute, box));
    if (a) ++overlap_hits;

    float ya = 0.0f;
    float yb = 0.0f;
    bool ra = map_ramp_surface(*indexed, p.x, p.z, &ya);
    CHECK(ra == map_ramp_surface(*brute, p.x, p.z, &yb));
    if (ra) {
      CHECK_NEAR(ya, yb, 0.0001f);
      ++ramp_hits;
    }

    // The two height-sensitive ramp queries: one bounded above by stepping
    // reach, one bounded by the player's own hull.
    bool na = map_ramp_surface_near(*indexed, p.x, p.z, p.y, &ya);
    CHECK(na == map_ramp_surface_near(*brute, p.x, p.z, p.y, &yb));
    if (na) {
      CHECK_NEAR(ya, yb, 0.0001f);
      ++near_hits;
    }
    CHECK(map_ramp_blocks(*indexed, p, (i & 1) != 0) ==
          map_ramp_blocks(*brute, p, (i & 1) != 0));

    // Near-horizontal rays are the case the per-cell band targets: they cross
    // many cells while staying inside one or two bands.
    float pitch = (i % 3 == 0) ? rng_float(rng, -0.02f, 0.02f) : rng_float(rng, -1.4f, 1.4f);
    Vec3 dir = angles_forward(rng_float(rng, -PI, PI), pitch);
    float ta = ray_map(*indexed, p, dir, 150.0f);
    float tb = ray_map(*brute, p, dir, 150.0f);
    CHECK_NEAR(ta, tb, 0.001f);
    if (tb < 150.0f) ++ray_hits;
  }

  CHECK(overlap_hits > 100);
  CHECK(ramp_hits > 100);
  CHECK(near_hits > 100);
  CHECK(ray_hits > 3000);
}

// The band filter has two code paths - consulted or ignored, chosen once per
// query from MapGrid::use_bands - and both must answer identically. Running the
// same busy map both ways covers the small-map path on geometry big enough to
// actually exercise it.
TEST(map_grid_band_filter_matches_unfiltered) {
  auto filtered = std::make_unique<Map>();
  std::string text = tall_map_text();
  CHECK(map_parse(text.c_str(), filtered.get()));
  CHECK(filtered->grid.built);
  CHECK(filtered->grid.use_bands);  // the map is big enough to turn them on

  auto plain = std::make_unique<Map>(*filtered);
  plain->grid.use_bands = false;

  Rng rng{0x0DDBA11};
  int overlap_hits = 0;
  int ray_hits = 0;
  for (int i = 0; i < 4000; ++i) {
    Vec3 p{rng_float(rng, -32.0f, 32.0f), rng_float(rng, -14.0f, 44.0f),
           rng_float(rng, -32.0f, 32.0f)};
    bool crouch = (i & 1) != 0;

    Aabb box = player_aabb(p, crouch);
    bool a = map_box_overlap(*filtered, box);
    CHECK(a == map_box_overlap(*plain, box));
    if (a) ++overlap_hits;

    float ya = 0.0f;
    float yb = 0.0f;
    CHECK(map_ramp_surface_near(*filtered, p.x, p.z, p.y, &ya) ==
          map_ramp_surface_near(*plain, p.x, p.z, p.y, &yb));
    CHECK_NEAR(ya, yb, 0.0001f);
    CHECK(map_ramp_blocks(*filtered, p, crouch) == map_ramp_blocks(*plain, p, crouch));

    Vec3 dir = angles_forward(rng_float(rng, -PI, PI), rng_float(rng, -1.4f, 1.4f));
    float ta = ray_map(*filtered, p, dir, 150.0f);
    float tb = ray_map(*plain, p, dir, 150.0f);
    CHECK_NEAR(ta, tb, 0.001f);
    if (tb < 150.0f) ++ray_hits;
  }
  CHECK(overlap_hits > 100);
  CHECK(ray_hits > 2000);
}

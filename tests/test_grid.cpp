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

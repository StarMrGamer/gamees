#include "test_harness.h"

#include "core/rng.h"
#include "game/collision.h"
#include "game/map.h"
#include "game/tuning.h"
#include "render/mesh.h"

#include <cmath>
#include <memory>
#include <string>

namespace {

std::string box_brush_text(Vec3 lo, Vec3 hi) {
  char buf[512];
  std::snprintf(buf, sizeof(buf),
                "brush 0.5 0.5 0.5\n"
                "plane  1  0  0 %.3f\n"
                "plane -1  0  0 %.3f\n"
                "plane  0  1  0 %.3f\n"
                "plane  0 -1  0 %.3f\n"
                "plane  0  0  1 %.3f\n"
                "plane  0  0 -1 %.3f\n",
                hi.x, -lo.x, hi.y, -lo.y, hi.z, -lo.z);
  return buf;
}

Aabb probe_at(Vec3 p) {
  return {{p.x - 0.05f, p.y - 0.05f, p.z - 0.05f}, {p.x + 0.05f, p.y + 0.05f, p.z + 0.05f}};
}

}  // namespace

// A brush whose planes describe an axis-aligned box must behave exactly like a
// box. This is the base case: if it does not hold, nothing built on it will.
TEST(brush_shaped_like_a_box_matches_a_box) {
  const Vec3 lo{-3.0f, 0.0f, -2.0f};
  const Vec3 hi{4.0f, 2.5f, 5.0f};

  auto as_brush = std::make_unique<Map>();
  std::string bt = "name brushbox\nspawn 0 20 0 0\n" + box_brush_text(lo, hi);
  CHECK(map_parse(bt.c_str(), as_brush.get()));
  CHECK_EQ_INT(as_brush->brush_count, 1);

  auto as_box = std::make_unique<Map>();
  char boxline[256];
  std::snprintf(boxline, sizeof(boxline), "name plainbox\nspawn 0 20 0 0\nbox %.3f %.3f %.3f %.3f %.3f %.3f 0.5 0.5 0.5\n",
                lo.x, lo.y, lo.z, hi.x - lo.x, hi.y - lo.y, hi.z - lo.z);
  CHECK(map_parse(boxline, as_box.get()));
  CHECK_EQ_INT(as_box->box_count, 1);

  // Bounds recovered from the plane set must match the box exactly.
  CHECK_NEAR(as_brush->brushes[0].min.x, lo.x, 0.001f);
  CHECK_NEAR(as_brush->brushes[0].min.y, lo.y, 0.001f);
  CHECK_NEAR(as_brush->brushes[0].min.z, lo.z, 0.001f);
  CHECK_NEAR(as_brush->brushes[0].max.x, hi.x, 0.001f);
  CHECK_NEAR(as_brush->brushes[0].max.y, hi.y, 0.001f);
  CHECK_NEAR(as_brush->brushes[0].max.z, hi.z, 0.001f);

  Rng rng{0xB0BULL};
  int inside_hits = 0;
  for (int i = 0; i < 6000; ++i) {
    Vec3 p{rng_float(rng, -8.0f, 9.0f), rng_float(rng, -4.0f, 7.0f), rng_float(rng, -7.0f, 10.0f)};
    Aabb probe = probe_at(p);
    bool a = map_box_overlap(*as_brush, probe);
    bool b = map_box_overlap(*as_box, probe);
    CHECK(a == b);
    if (b) ++inside_hits;

    Vec3 dir = angles_forward(rng_float(rng, -PI, PI), rng_float(rng, -1.3f, 1.3f));
    CHECK_NEAR(ray_map(*as_brush, p, dir, 60.0f), ray_map(*as_box, p, dir, 60.0f), 0.01f);
  }
  CHECK(inside_hits > 200);
}

// The whole point of the primitive: a diagonal wall must leave the space behind
// it open. As a bounding box this brush would fill its entire 10x10 footprint,
// which is what used to seal off corners on imported maps.
TEST(diagonal_wall_brush_leaves_its_corner_open) {
  auto map = std::make_unique<Map>();
  // Inside is the half of the box where x + z <= 0.
  std::string text =
      "name wedge\n"
      "spawn 0 20 0 0\n"
      "brush 0.5 0.4 0.4\n"
      "plane  1  0  0 5\n"
      "plane -1  0  0 5\n"
      "plane  0  1  0 4\n"
      "plane  0 -1  0 0\n"
      "plane  0  0  1 5\n"
      "plane  0  0 -1 5\n"
      "plane  0.7071 0 0.7071 0\n";
  CHECK(map_parse(text.c_str(), map.get()));
  CHECK_EQ_INT(map->brush_count, 1);

  // Bounding box still spans the full footprint...
  CHECK_NEAR(map->brushes[0].min.x, -5.0f, 0.01f);
  CHECK_NEAR(map->brushes[0].max.x, 5.0f, 0.01f);
  CHECK_NEAR(map->brushes[0].min.z, -5.0f, 0.01f);
  CHECK_NEAR(map->brushes[0].max.z, 5.0f, 0.01f);

  // ...but the far corner is open, where a bounding box would have been solid.
  CHECK(!map_box_overlap(*map, probe_at({3.5f, 1.0f, 3.5f})));
  CHECK(!map_box_overlap(*map, probe_at({4.5f, 2.0f, 4.5f})));
  // And the near corner is solid.
  CHECK(map_box_overlap(*map, probe_at({-3.5f, 1.0f, -3.5f})));
  CHECK(map_box_overlap(*map, probe_at({-4.5f, 2.0f, -4.5f})));

  // Sampled agreement between the overlap test and plain containment.
  Rng rng{0x0DD};
  int solid = 0;
  int open = 0;
  for (int i = 0; i < 8000; ++i) {
    Vec3 p{rng_float(rng, -6.0f, 6.0f), rng_float(rng, 0.2f, 3.8f), rng_float(rng, -6.0f, 6.0f)};
    bool contained = map_brush_contains(map->brushes[0], p);
    if (contained) {
      ++solid;
      CHECK(map_box_overlap(*map, probe_at(p)));
    } else if (p.x + p.z > 1.0f) {
      // Clear of the diagonal face, so the expanded-plane test must agree.
      ++open;
      CHECK(!map_box_overlap(*map, probe_at(p)));
    }
  }
  CHECK(solid > 500);
  CHECK(open > 500);
}

// Ray-vs-brush is an exact interval clip, so it must agree with the analytic
// answer for a shape we can reason about.
TEST(ray_brush_hits_the_diagonal_face) {
  auto map = std::make_unique<Map>();
  std::string text =
      "name wedge\n"
      "spawn 0 20 0 0\n"
      "brush 0.5 0.4 0.4\n"
      "plane  1  0  0 5\n"
      "plane -1  0  0 5\n"
      "plane  0  1  0 4\n"
      "plane  0 -1  0 0\n"
      "plane  0  0  1 5\n"
      "plane  0  0 -1 5\n"
      "plane  0.7071 0 0.7071 0\n";
  CHECK(map_parse(text.c_str(), map.get()));

  // Fired along -x from outside at z = 0, the diagonal face x + z = 0 is hit
  // at x = 0, so the ray travels 8 units from x = 8.
  float t = ray_map(*map, {8.0f, 1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, 50.0f);
  CHECK_NEAR(t, 8.0f, 0.02f);

  // Fired from the open corner outward, nothing is in the way.
  CHECK_NEAR(ray_map(*map, {4.0f, 1.0f, 4.0f}, {1.0f, 0.0f, 0.0f}, 50.0f), 50.0f, 0.01f);

  // Straight down through the solid half lands on the top face at y = 4.
  CHECK_NEAR(ray_map(*map, {-3.0f, 10.0f, -3.0f}, {0.0f, -1.0f, 0.0f}, 50.0f), 6.0f, 0.02f);
}

// Bevel planes are appended so the swept test stays tight; a brush that already
// carries a face must not have it duplicated.
TEST(brush_finalize_appends_bevel_planes) {
  auto map = std::make_unique<Map>();
  std::string text = "name b\nspawn 0 20 0 0\n" + box_brush_text({-1, -1, -1}, {1, 1, 1});
  CHECK(map_parse(text.c_str(), map.get()));
  // All six faces were already axis-aligned bevels, so nothing should be added.
  CHECK_EQ_INT(map->brushes[0].plane_count, 6);

  auto wedge = std::make_unique<Map>();
  std::string wt =
      "name w\nspawn 0 20 0 0\n"
      "brush 0.5 0.5 0.5\n"
      "plane  1  0  0 5\n"
      "plane -1  0  0 5\n"
      "plane  0  1  0 4\n"
      "plane  0 -1  0 0\n"
      "plane  0  0  1 5\n"
      "plane  0  0 -1 5\n"
      "plane  0.7071 0 0.7071 0\n";
  CHECK(map_parse(wt.c_str(), wedge.get()));
  // Seven given planes, all six bevels already present among them.
  CHECK_EQ_INT(wedge->brushes[0].plane_count, 7);
}

// A plane set that does not close into a solid must be rejected, not stored.
TEST(brush_that_does_not_bound_a_solid_is_dropped) {
  auto map = std::make_unique<Map>();
  std::string text =
      "name open\n"
      "spawn 0 20 0 0\n"
      "box -10 -1 -10 20 1 20 0.5 0.5 0.5\n"
      "brush 0.5 0.5 0.5\n"
      "plane 1 0 0 5\n"
      "plane 0 1 0 5\n"
      "plane 0 0 1 5\n"
      "plane 0 0 -1 5\n";
  CHECK(map_parse(text.c_str(), map.get()));
  CHECK_EQ_INT(map->brush_count, 0);
  CHECK_EQ_INT(map->box_count, 1);
}

// The face polygons are recovered by clipping, which is easy to get subtly
// wrong. Surface area is a single number that catches a dropped face, a
// doubled face, or a face clipped to the wrong extent.
static float mesh_surface_area(const MeshBuilder& mb) {
  float area = 0.0f;
  for (size_t i = 0; i + 2 < mb.verts.size(); i += 3) {
    Vec3 a = mb.verts[i].pos;
    Vec3 b = mb.verts[i + 1].pos;
    Vec3 c = mb.verts[i + 2].pos;
    area += vec3_length(vec3_cross(b - a, c - a)) * 0.5f;
  }
  return area;
}

TEST(brush_mesh_matches_box_surface_area) {
  auto map = std::make_unique<Map>();
  std::string text = "name b\nspawn 0 20 0 0\n" + box_brush_text({-3, 0, -2}, {4, 2.5f, 5});
  CHECK(map_parse(text.c_str(), map.get()));

  MeshBuilder mb;
  mb.add_brush(map->brushes[0]);
  CHECK(!mb.verts.empty());

  const float w = 7.0f, h = 2.5f, d = 7.0f;
  CHECK_NEAR(mesh_surface_area(mb), 2.0f * (w * h + w * d + h * d), 0.05f);

  // Same shape via the box path: identical area, and identical triangle count.
  MeshBuilder bb;
  bb.add_box({-3, 0, -2}, {4, 2.5f, 5}, {0.5f, 0.5f, 0.5f});
  CHECK_NEAR(mesh_surface_area(mb), mesh_surface_area(bb), 0.05f);
  CHECK_EQ_INT(static_cast<int>(mb.verts.size()), static_cast<int>(bb.verts.size()));
}

TEST(brush_mesh_closes_a_wedge) {
  auto map = std::make_unique<Map>();
  std::string text =
      "name w\nspawn 0 20 0 0\n"
      "brush 0.5 0.5 0.5\n"
      "plane  1  0  0 5\n"
      "plane -1  0  0 5\n"
      "plane  0  1  0 4\n"
      "plane  0 -1  0 0\n"
      "plane  0  0  1 5\n"
      "plane  0  0 -1 5\n"
      "plane  0.7071 0 0.7071 0\n";
  CHECK(map_parse(text.c_str(), map.get()));

  MeshBuilder mb;
  mb.add_brush(map->brushes[0]);
  CHECK(!mb.verts.empty());

  // Half of a 10x10 footprint, 4 tall, cut corner to corner:
  //   two triangles 10x10/2, two walls 10x4, one diagonal face 4 x 10*sqrt(2).
  float expected = 2.0f * (0.5f * 10.0f * 10.0f)      // top and bottom
                 + 2.0f * (10.0f * 4.0f)              // the two square sides
                 + 4.0f * (10.0f * std::sqrt(2.0f));  // the diagonal face
  CHECK_NEAR(mesh_surface_area(mb), expected, 0.2f);
}

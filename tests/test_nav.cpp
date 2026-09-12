#include "core/rng.h"
#include "game/collision.h"
#include "game/map.h"
#include "game/map_check.h"
#include "game/nav.h"
#include "game/tuning.h"
#include "test_harness.h"

#include <cmath>
#include <memory>
#include <string>

namespace {

// Two rooms joined only by a doorway at one end. A straight line between them
// goes through the dividing wall, so any route that works has to go round -
// which is the entire reason the navmesh exists.
std::string two_rooms() {
  return "name tworooms\n"
         "box -20 -1 -20 40 1 40 0.5 0.5 0.5\n"     // floor
         "box -20 0 -20 40 6 1 0.4 0.4 0.4\n"       // outer walls
         "box -20 0 19 40 6 1 0.4 0.4 0.4\n"
         "box -20 0 -20 1 6 40 0.4 0.4 0.4\n"
         "box 19 0 -20 1 6 40 0.4 0.4 0.4\n"
         // Divider down the middle, with a gap at the far +z end only.
         "box -0.5 0 -20 1 6 34 0.3 0.3 0.3\n"
         "spawn -10 1 -10 90\n"
         "spawn 10 1 -10 270\n";
}

// A room plus a sealed box with no way in. Nothing inside may be routable.
std::string room_with_island() {
  return "name island\n"
         "box -20 -1 -20 40 1 40 0.5 0.5 0.5\n"
         "box -20 0 -20 40 6 1 0.4 0.4 0.4\n"
         "box -20 0 19 40 6 1 0.4 0.4 0.4\n"
         "box -20 0 -20 1 6 40 0.4 0.4 0.4\n"
         "box 19 0 -20 1 6 40 0.4 0.4 0.4\n"
         // A floating platform nothing connects to.
         "box 10 12 10 6 1 6 0.7 0.2 0.2\n"
         "spawn -10 1 -10 90\n";
}

}  // namespace

TEST(nav_builds_from_the_reachability_walk) {
  auto map = std::make_unique<Map>();
  std::string text = two_rooms();
  CHECK(map_parse(text.c_str(), map.get()));
  CHECK(!map->nav.built);  // map_load must not pay for this
  map_build_nav(map.get());
  CHECK(map->nav.built);
  CHECK(map->nav.complete);
  CHECK(map->nav.node_count > 20);
  CHECK(map->nav.link_count > 20);

  // Every node has to be somewhere a player can actually be. A node inside
  // geometry is a route that wedges whoever follows it.
  for (int i = 0; i < map->nav.node_count; ++i) {
    Vec3 p = map->nav.nodes[i];
    CHECK(!map_box_overlap(*map, player_aabb(p, false)));
  }

  // And every node must be joined to something, or it is a trap to route into.
  for (int i = 0; i < map->nav.node_count; ++i) {
    CHECK(map->nav.link_start[i + 1] > map->nav.link_start[i]);
  }
}

// The headline case: a route between two rooms whose straight line is a wall.
TEST(nav_routes_around_a_wall) {
  auto map = std::make_unique<Map>();
  std::string text = two_rooms();
  CHECK(map_parse(text.c_str(), map.get()));
  map_build_nav(map.get());

  Vec3 left{-10.0f, 0.0f, -10.0f};
  Vec3 right{10.0f, 0.0f, -10.0f};

  // Confirm the premise: the direct line really is blocked, so this test is
  // not passing because the two points happen to see each other.
  Vec3 eye_l = left + Vec3{0.0f, EYE_HEIGHT, 0.0f};
  Vec3 d = right - left;
  float span = vec3_length(d);
  CHECK(ray_map(*map, eye_l, d / span, span) < span - 0.5f);

  NavPath path;
  CHECK(nav_find_path(*map, left, right, &path));
  CHECK(path.count >= 2);

  // Consecutive waypoints must be genuinely linked, not merely near.
  for (int i = 0; i + 1 < path.count; ++i) {
    int a = path.nodes[i];
    int b = path.nodes[i + 1];
    bool linked = false;
    for (int32_t e = map->nav.link_start[a]; e < map->nav.link_start[a + 1]; ++e) {
      if (map->nav.links[e] == b) linked = true;
    }
    CHECK(linked);
  }

  // The route has to actually go the long way round, not cut through.
  float straight = vec3_length(right - left);
  float along = 0.0f;
  for (int i = 0; i + 1 < path.count; ++i) {
    along += vec3_length(map->nav.nodes[path.nodes[i + 1]] - map->nav.nodes[path.nodes[i]]);
  }
  CHECK(along > straight * 1.5f);
}

TEST(nav_refuses_an_unreachable_island) {
  auto map = std::make_unique<Map>();
  std::string text = room_with_island();
  CHECK(map_parse(text.c_str(), map.get()));
  map_build_nav(map.get());
  CHECK(map->nav.built);

  // No node may sit on the floating platform: the walk cannot get there, so
  // the mesh must not claim it can.
  int on_island = 0;
  for (int i = 0; i < map->nav.node_count; ++i) {
    if (map->nav.nodes[i].y > 8.0f) ++on_island;
  }
  CHECK_EQ_INT(on_island, 0);

  NavPath path;
  // Asking for a route to the island lands on the nearest *reachable* node
  // instead, which must not be up there.
  if (nav_find_path(*map, {-10.0f, 0.0f, -10.0f}, {13.0f, 13.0f, 13.0f}, &path)) {
    CHECK(path.count > 0);
    CHECK(map->nav.nodes[path.nodes[path.count - 1]].y < 8.0f);
  }
}

// An unbuilt mesh must fail queries rather than return nonsense, because the
// agent's fallback depends on being told.
TEST(nav_queries_fail_cleanly_when_unbuilt) {
  auto map = std::make_unique<Map>();
  std::string text = two_rooms();
  CHECK(map_parse(text.c_str(), map.get()));
  CHECK(!map->nav.built);
  CHECK_EQ_INT(nav_nearest_node(*map, {0.0f, 0.0f, 0.0f}), -1);
  NavPath path;
  CHECK(!nav_find_path(*map, {-10.0f, 0.0f, -10.0f}, {10.0f, 0.0f, -10.0f}, &path));
  CHECK_EQ_INT(path.count, 0);
}

// Building twice must give the same mesh, not append to the old one.
TEST(nav_rebuild_is_idempotent) {
  auto map = std::make_unique<Map>();
  std::string text = two_rooms();
  CHECK(map_parse(text.c_str(), map.get()));
  map_build_nav(map.get());
  int nodes = map->nav.node_count;
  int links = map->nav.link_count;
  map_build_nav(map.get());
  CHECK_EQ_INT(map->nav.node_count, nodes);
  CHECK_EQ_INT(map->nav.link_count, links);
}

// The walk that feeds the navmesh is the same one the leak checker uses. If a
// visitor could change its answer, every map report would depend on who asked.
TEST(nav_visitor_does_not_change_the_walk) {
  auto map = std::make_unique<Map>();
  std::string text = two_rooms();
  CHECK(map_parse(text.c_str(), map.get()));
  MapReachReport plain = map_check_reachable_leaks(*map);
  MapReachReport visited = map_walk_reachable(*map, nullptr);
  CHECK_EQ_INT(plain.reachable_positions, visited.reachable_positions);
  CHECK_EQ_INT(plain.leak_columns, visited.leak_columns);

  struct Counter : ReachVisitor {
    int nodes = 0;
    int edges = 0;
    void node(Vec3) override { ++nodes; }
    void edge(Vec3, Vec3) override { ++edges; }
  } counter;
  MapReachReport hooked = map_walk_reachable(*map, &counter);
  CHECK_EQ_INT(hooked.reachable_positions, plain.reachable_positions);
  CHECK_EQ_INT(counter.nodes, plain.reachable_positions);
  CHECK(counter.edges > 0);
}

namespace {

// A deck with open space underneath, reached by steps off to one side. A point
// in the tunnel and a point on the deck directly above it are metres apart
// vertically but the same place in plan view - which is the case that decides
// whether the mesh understands height at all.
std::string bridge_and_tunnel() {
  return "name bridge\n"
         "box -20 -1 -20 40 1 40 0.5 0.5 0.5\n"    // floor
         "box -20 0 -20 40 9 1 0.4 0.4 0.4\n"      // outer walls
         "box -20 0 19 40 9 1 0.4 0.4 0.4\n"
         "box -20 0 -20 1 9 40 0.4 0.4 0.4\n"
         "box 19 0 -20 1 9 40 0.4 0.4 0.4\n"
         "box -5 5 -20 9 1 40 0.6 0.6 0.3\n"       // deck, open beneath
         "box 4 0 -4 2 6 8 0.3 0.3 0.3\n"          // steps up to it
         "box 6 0 -4 2 5 8 0.3 0.3 0.3\n"
         "box 8 0 -4 2 4 8 0.3 0.3 0.3\n"
         "box 10 0 -4 2 3 8 0.3 0.3 0.3\n"
         "box 12 0 -4 2 2 8 0.3 0.3 0.3\n"
         "box 14 0 -4 2 1 8 0.3 0.3 0.3\n"
         "spawn 17 1 8 270\n";
}

}  // namespace

// The mesh has to keep a tunnel and the deck over it apart. Collapsing them
// into one node is the classic 2D-navmesh failure: a bot under the bridge
// believes it has already arrived at a waypoint six metres above its head.
TEST(nav_separates_levels_in_the_same_column) {
  auto map = std::make_unique<Map>();
  std::string text = bridge_and_tunnel();
  CHECK(map_parse(text.c_str(), map.get()));
  map_build_nav(map.get());
  CHECK(map->nav.built);

  const Vec3 tunnel{0.0f, 0.0f, 0.0f};
  const Vec3 deck{0.0f, 6.0f, 0.0f};

  // The premise: both are standable, and they really are stacked.
  CHECK(map_grounded_at(*map, tunnel, false));
  CHECK(map_grounded_at(*map, deck, false));
  CHECK(!map_box_overlap(*map, player_aabb(tunnel, false)));
  CHECK(!map_box_overlap(*map, player_aabb(deck, false)));

  int under = nav_nearest_node(*map, tunnel);
  int over = nav_nearest_node(*map, deck);
  CHECK(under >= 0 && over >= 0);
  CHECK(under != over);
  CHECK(map->nav.nodes[under].y < 3.0f);
  CHECK(map->nav.nodes[over].y > 4.0f);

  // Getting from one to the other means walking to the steps and back, so the
  // route is far longer than the six metres that separate them.
  NavPath path;
  CHECK(nav_find_path(*map, tunnel, deck, &path));
  CHECK(path.count >= 4);
  float along = 0.0f;
  for (int i = 0; i + 1 < path.count; ++i) {
    along += vec3_length(map->nav.nodes[path.nodes[i + 1]] - map->nav.nodes[path.nodes[i]]);
  }
  CHECK(along > 20.0f);
}

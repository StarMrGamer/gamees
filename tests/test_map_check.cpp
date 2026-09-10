#include "test_harness.h"

#include "game/map.h"
#include "game/map_check.h"
#include "game/tuning.h"

#include <memory>
#include <string>

namespace {

// A walled room whose floor is a ring of four slabs, leaving a square hole in
// the middle. `hole_fill` optionally plugs it; `gap` narrows the hole to a slot
// the player hull cannot fit through.
std::string room_map(const char* extra) {
  std::string t =
      "name checkroom\n"
      // Floor ring around a 4x4 hole centred on the origin.
      "box -10 0 -10 20 1 8 0.5 0.5 0.5\n"
      "box -10 0 2 20 1 8 0.5 0.5 0.5\n"
      "box -10 0 -2 8 1 4 0.5 0.5 0.5\n"
      "box 2 0 -2 8 1 4 0.5 0.5 0.5\n"
      // Perimeter walls, so the only way out of the world is the hole.
      "box -10 1 -10 20 4 0.5 0.4 0.4 0.4\n"
      "box -10 1 9.5 20 4 0.5 0.4 0.4 0.4\n"
      "box -10 1 -10 0.5 4 20 0.4 0.4 0.4\n"
      "box 9.5 1 -10 0.5 4 20 0.4 0.4 0.4\n"
      "spawn -6 1 -6 0\n";
  if (extra) t += extra;
  return t;
}

}  // namespace

// The walk check must actually find a hole you can fall through.
TEST(map_check_finds_a_reachable_hole) {
  auto map = std::make_unique<Map>();
  std::string text = room_map(nullptr);
  CHECK(map_parse(text.c_str(), map.get()));

  MapReachReport rep = map_check_reachable_leaks(*map);
  CHECK(rep.ran);
  CHECK(rep.reachable_positions > 100);
  CHECK(rep.leak_columns > 0);
  CHECK(rep.spawns_unsupported == 0);
}

// Plug the hole and the same map must come back clean.
TEST(map_check_sealed_room_has_no_leaks) {
  auto map = std::make_unique<Map>();
  std::string text = room_map("box -2 0 -2 4 1 4 0.5 0.5 0.5\n");
  CHECK(map_parse(text.c_str(), map.get()));

  MapReachReport rep = map_check_reachable_leaks(*map);
  CHECK(rep.ran);
  CHECK(rep.reachable_positions > 100);
  CHECK_EQ_INT(rep.leak_columns, 0);
}

// The point of resolving each step with the real physics rather than a voxel
// model: the player hull is 0.6 m wide, so a 0.4 m slot is not a hole. A
// point-sample check would report this as a leak.
TEST(map_check_ignores_a_gap_narrower_than_the_player) {
  auto map = std::make_unique<Map>();
  std::string text =
      "name slot\n"
      "box -10 0 -10 9.8 1 20 0.5 0.5 0.5\n"   // floor up to x = -0.2
      "box 0.2 0 -10 9.8 1 20 0.5 0.5 0.5\n"   // floor from x = +0.2
      "box -10 1 -10 20 4 0.5 0.4 0.4 0.4\n"
      "box -10 1 9.5 20 4 0.5 0.4 0.4 0.4\n"
      "box -10 1 -10 0.5 4 20 0.4 0.4 0.4\n"
      "box 9.5 1 -10 0.5 4 20 0.4 0.4 0.4\n"
      "spawn -5 1 0 0\n";
  CHECK(map_parse(text.c_str(), map.get()));

  MapReachReport rep = map_check_reachable_leaks(*map);
  CHECK(rep.ran);
  // The player straddles the 0.4 m slot and stays supported, so walking across
  // it is safe and the map has no leak.
  CHECK_EQ_INT(rep.leak_columns, 0);
}

// A spawn floating over nothing is its own kind of broken map.
TEST(map_check_reports_an_unsupported_spawn) {
  auto map = std::make_unique<Map>();
  std::string text =
      "name floating\n"
      "box -10 0 -10 20 1 20 0.5 0.5 0.5\n"
      "spawn 400 40 400 0\n";
  CHECK(map_parse(text.c_str(), map.get()));

  MapReachReport rep = map_check_reachable_leaks(*map);
  CHECK(rep.ran);
  CHECK(rep.spawns_unsupported == 1);
}

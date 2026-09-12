#pragma once

#include "game/map.h"

// Result of a "leak to the void" check: the map is voxelised and the empty
// space is flood-filled from outside the geometry. If that exterior flood
// reaches a point of interest (e.g. a spawn), the map has a gap to the void.
struct MapCheckReport {
  bool ran = false;
  bool leaked = false;
  int point_count = 0;
  int leaked_points = 0;
  Vec3 first_leak{};
  float voxel_size = 0.0f;
  int grid_x = 0, grid_y = 0, grid_z = 0;
  long long solid_cells = 0;
  long long exterior_cells = 0;
};

// `points` are the places that must stay sealed (spawns). The check is
// conservative: it uses a coarse voxel grid and treats ramps as solid below
// their surface.
MapCheckReport map_check_leaks(const Map& map, const Vec3* points, int point_count);

// A stronger check than the voxel flood above, and the one that actually
// answers "can a player fall out of this map?".
//
// It walks the map the way a player does: a breadth-first search over standing
// positions on a horizontal grid, where every candidate step is resolved with
// the real move_slide() physics rather than a voxel approximation. That means
// the player's true hull width, step height and ramp handling all apply, so a
// gap narrower than the player is correctly *not* a leak.
//
// A leak is a reachable position from which stepping to a neighbour drops the
// player below map.void_y - i.e. somewhere you can walk to and fall out of the
// world.
struct MapReachReport {
  bool ran = false;
  int reachable_positions = 0;
  int leak_columns = 0;
  Vec3 first_leak{};
  // Up to `capacity` distinct leak positions, for reporting.
  static constexpr int MAX_REPORTED = 512;
  Vec3 leaks[MAX_REPORTED];
  int leaks_reported = 0;
  int spawns_unsupported = 0;  // spawns that are not standing on anything
};

MapReachReport map_check_reachable_leaks(const Map& map);

// The walk above, with a hook. Anything that needs to know where a player can
// actually go - the leak check, the navmesh - must go through this one search,
// or the two answers drift and the bot ends up routed through a wall the leak
// checker knows is solid.
// The tallest step the reachability walk will climb. Exposed because the
// navmesh needs the same number: it decides whether a step discovered in one
// direction can be assumed to work in reverse.
constexpr float REACH_CLIMB_HEIGHT = 1.225f;

struct ReachVisitor {
  virtual ~ReachVisitor() = default;
  // A standing position the search reached.
  virtual void node(Vec3 pos) { (void)pos; }
  // A step the player physics accepted, from one standing position to another.
  virtual void edge(Vec3 from, Vec3 to) { (void)from; (void)to; }
};

MapReachReport map_walk_reachable(const Map& map, ReachVisitor* visitor);

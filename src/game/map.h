#pragma once

#include "core/math.h"

#include <cstdint>

constexpr int MAX_MAP_BOXES = 4096;
constexpr int MAX_MAP_RAMPS = 1024;
constexpr int MAX_SPAWNS = 16;

// Uniform grid over the map's XZ footprint, used to prune collision and
// raycast queries. Arena maps are wide and flat, so indexing the two
// horizontal axes prunes almost as well as a full octree at a fraction of the
// complexity, and it stays a plain POD so Map remains memcpy-able.
constexpr int MAP_GRID_DIM = 64;
constexpr int MAP_GRID_CELLS = MAP_GRID_DIM * MAP_GRID_DIM;
constexpr int MAP_GRID_MAX_BOX_ENTRIES = 49152;
constexpr int MAP_GRID_MAX_RAMP_ENTRIES = 16384;

struct MapBox {
  Vec3 min, max;
  Vec3 color;
};

// A right-triangular prism (wedge) whose top surface slopes from `min.y` at
// the low edge up to `max.y` at the high edge. `dir` is the ascending
// direction: 0=+x, 1=-x, 2=+z, 3=-z. The solid sits below the slope.
struct MapRamp {
  Vec3 min, max;
  Vec3 color;
  uint8_t dir;
};

// CSR-style bucket lists: cell c owns entries [start[c], start[c + 1]). A
// primitive is listed in every cell its XZ extent touches, so a query only has
// to visit the cells it overlaps. `built` is false when the map does not fit
// the entry budget, in which case every query falls back to a full scan and
// stays correct, just slower.
struct MapGrid {
  bool built;
  float org_x, org_z;          // world position of cell (0, 0)
  float inv_cell_x, inv_cell_z;
  int32_t box_start[MAP_GRID_CELLS + 1];
  int32_t ramp_start[MAP_GRID_CELLS + 1];
  uint16_t box_entries[MAP_GRID_MAX_BOX_ENTRIES];
  uint16_t ramp_entries[MAP_GRID_MAX_RAMP_ENTRIES];
};

struct Map {
  char name[32];
  MapBox boxes[MAX_MAP_BOXES];
  int box_count;
  MapRamp ramps[MAX_MAP_RAMPS];
  int ramp_count;
  Vec3 spawns[MAX_SPAWNS];
  float spawn_yaws[MAX_SPAWNS];
  int spawn_count;
  Vec3 health_spawns[MAX_SPAWNS];
  int health_count;
  Vec3 light_dir;
  Vec3 fog_color;
  float fog_density;
  Vec3 sky_color;
  Vec3 sky_zenith;
  // Falling below this height means the player left the map; they are
  // teleported back to a spawn with zero momentum.
  float void_y;
  MapGrid grid;
};

// Builds `out->grid` from the map's boxes and ramps. map_parse() calls this
// already; only call it directly after mutating geometry by hand.
void map_build_grid(Map* out);

bool map_parse(const char* text, Map* out);
bool map_load(const char* path, Map* out);

// Register a fallback root tried (in order) when a relative map path does not
// resolve against the current working directory. main() registers the
// executable's directory and its parents so the game/data layout works no
// matter where the binary is launched from.
void map_add_search_dir(const char* dir);

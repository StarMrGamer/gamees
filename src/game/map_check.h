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

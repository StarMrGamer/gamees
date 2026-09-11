#pragma once

#include "core/math.h"

#include <cstdint>

constexpr int MAX_MAP_BOXES = 4096;
// Terrain arrives as ramps too (displacement cells, merged), so this budget
// covers both hand-placed slopes and imported ground - de_dust2 alone needs
// ~1000 for terrain after merging.
constexpr int MAX_MAP_RAMPS = 4096;
constexpr int MAX_MAP_BRUSHES = 1024;
// Planes per convex brush. A census of de_dust2 found 88% of its angled solids
// use four or fewer angled faces; sixteen leaves room for those plus the six
// axis-aligned bevel planes every brush carries (see MapBrush).
constexpr int MAX_BRUSH_PLANES = 16;
constexpr int MAX_SPAWNS = 16;

// Uniform grid over the map's XZ footprint, used to prune collision and
// raycast queries. Arena maps are wide and flat, so indexing the two
// horizontal axes prunes almost as well as a full octree at a fraction of the
// complexity, and it stays a plain POD so Map remains memcpy-able.
constexpr int MAP_GRID_DIM = 64;
constexpr int MAP_GRID_CELLS = MAP_GRID_DIM * MAP_GRID_DIM;
constexpr int MAP_GRID_MAX_BOX_ENTRIES = 49152;
constexpr int MAP_GRID_MAX_RAMP_ENTRIES = 65536;
constexpr int MAP_GRID_MAX_BRUSH_ENTRIES = 32768;

struct MapBox {
  Vec3 min, max;
  Vec3 color;
};

// A walkable slope: the solid below `slope_n . p = slope_d`, bounded by
// [min, max].
//
// The plane is stored rather than inferred from the bounds. Deriving it - "the
// surface runs from min.y at one edge to max.y at the other" - is only true for
// a wedge that fills its bounding box corner to corner, and most imported
// slopes do not: on de_dust2, 189 of 326 had a slope that started somewhere
// other than the bottom of their bounds, one of them 5.7 m out. Storing the
// plane also allows slopes that ascend diagonally rather than along an axis.
struct MapRamp {
  Vec3 min, max;
  Vec3 color;
  Vec3 slope_n;
  float slope_d;
};

// Surface height of a ramp at (x, z), ignoring bounds. Callers clamp.
inline float map_ramp_plane_y(const MapRamp& r, float x, float z) {
  if (r.slope_n.y > -1e-6f && r.slope_n.y < 1e-6f) return r.min.y;
  return (r.slope_d - r.slope_n.x * x - r.slope_n.z * z) / r.slope_n.y;
}

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
  int32_t brush_start[MAP_GRID_CELLS + 1];
  uint16_t box_entries[MAP_GRID_MAX_BOX_ENTRIES];
  uint16_t ramp_entries[MAP_GRID_MAX_RAMP_ENTRIES];
  uint16_t brush_entries[MAP_GRID_MAX_BRUSH_ENTRIES];
};

// An arbitrary convex solid, stored as the intersection of half-spaces
// (`dot(n, p) <= d`). This is how brushes are expressed in the source formats
// the importer reads, so a diagonal wall survives as itself instead of being
// widened into its bounding box.
//
// The plane set always includes the six axis-aligned planes of the brush's own
// bounding box. They are redundant for a point-in-solid test, but they are what
// makes the swept-AABB test tight: without them, expanding the angled planes by
// the player's extent rounds the brush's edges outward and the player collides
// with thin air near them. Source calls these bevel planes.
struct MapBrush {
  Vec3 n[MAX_BRUSH_PLANES];
  float d[MAX_BRUSH_PLANES];
  Vec3 min, max;  // cached bounds, for the spatial grid and for meshing
  Vec3 color;
  uint8_t plane_count;
};

struct Map {
  char name[32];
  MapBox boxes[MAX_MAP_BOXES];
  int box_count;
  MapRamp ramps[MAX_MAP_RAMPS];
  int ramp_count;
  MapBrush brushes[MAX_MAP_BRUSHES];
  int brush_count;
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

// Fills in a brush's cached bounds from its plane set, and appends the six
// axis-aligned bevel planes. Returns false if the planes do not bound a solid.
bool map_brush_finalize(MapBrush* brush);

// True when `p` is inside the brush (on the surface counts as inside).
bool map_brush_contains(const MapBrush& b, Vec3 p);

bool map_parse(const char* text, Map* out);
bool map_load(const char* path, Map* out);

// Register a fallback root tried (in order) when a relative map path does not
// resolve against the current working directory. main() registers the
// executable's directory and its parents so the game/data layout works no
// matter where the binary is launched from.
void map_add_search_dir(const char* dir);

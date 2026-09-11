#pragma once

#include "game/map.h"

struct Aabb {
  Vec3 min, max;
};

Aabb player_aabb(Vec3 pos, bool crouching);
bool aabb_overlap(const Aabb& a, const Aabb& b);
bool ray_aabb(Vec3 origin, Vec3 dir, const Aabb& box, float max_t, float* t_out);
float ray_map(const Map& map, Vec3 origin, Vec3 dir, float max_t);
bool map_box_overlap(const Map& map, const Aabb& box);
// Highest ramp surface height at (x, z), or false if no ramp covers the point.
bool map_ramp_surface(const Map& map, float x, float z, float* y_out);
// Highest ramp surface at (x, z) that is within stepping reach of `y_ref`.
// Terrain stacks, so the plain form above (which returns the highest surface
// anywhere in the column) reports a hillside overhead instead of the ground
// underfoot.
bool map_ramp_surface_near(const Map& map, float x, float z, float y_ref, float* y_out);
// True when a ramp's solid body actually overlaps the player hull at `pos`.
bool map_ramp_blocks(const Map& map, Vec3 pos, bool crouching);
// True when a player standing at `pos` has something underfoot. This is the
// rule move_slide() uses; anything that reports on a position (the probe, map
// checks) must call it rather than re-derive it, because a second version of
// the rule diverges and then reports the engine's behaviour wrongly.
bool map_grounded_at(const Map& map, Vec3 pos, bool crouching);
// Axis-aligned box against a convex brush, and ray against a convex brush.
bool brush_box_overlap(const MapBrush& b, const Aabb& box);
bool ray_brush(const MapBrush& b, Vec3 origin, Vec3 dir, float max_t, float* t_out);

struct MoveResult {
  Vec3 pos;
  Vec3 vel;
  Vec3 wall_normal;
  bool on_ground;
  bool hit_ceiling;
};

MoveResult move_slide(const Map& map, Vec3 pos, Vec3 vel, bool crouching, float dt);

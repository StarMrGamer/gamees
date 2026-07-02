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

struct MoveResult {
  Vec3 pos;
  Vec3 vel;
  bool on_ground;
  bool hit_ceiling;
};

MoveResult move_slide(const Map& map, Vec3 pos, Vec3 vel, bool crouching, float dt);

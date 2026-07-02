#include "game/collision.h"

#include "game/tuning.h"

#include <algorithm>
#include <cmath>

Aabb player_aabb(Vec3 pos, bool crouching) {
  float h = crouching ? PLAYER_CROUCH_HEIGHT : PLAYER_HEIGHT;
  return {{pos.x - PLAYER_HALF_W, pos.y, pos.z - PLAYER_HALF_W},
          {pos.x + PLAYER_HALF_W, pos.y + h, pos.z + PLAYER_HALF_W}};
}

bool aabb_overlap(const Aabb& a, const Aabb& b) {
  return a.min.x < b.max.x && a.max.x > b.min.x &&
         a.min.y < b.max.y && a.max.y > b.min.y &&
         a.min.z < b.max.z && a.max.z > b.min.z;
}

static Aabb map_box_aabb(const MapBox& b) {
  return {b.min, b.max};
}

bool map_box_overlap(const Map& map, const Aabb& box) {
  for (int i = 0; i < map.box_count; ++i) {
    if (aabb_overlap(box, map_box_aabb(map.boxes[i]))) return true;
  }
  return false;
}

bool ray_aabb(Vec3 origin, Vec3 dir, const Aabb& box, float max_t, float* t_out) {
  float tmin = 0.0f;
  float tmax = max_t;
  const float o[3] = {origin.x, origin.y, origin.z};
  const float d[3] = {dir.x, dir.y, dir.z};
  const float mn[3] = {box.min.x, box.min.y, box.min.z};
  const float mx[3] = {box.max.x, box.max.y, box.max.z};

  for (int axis = 0; axis < 3; ++axis) {
    if (std::fabs(d[axis]) < 0.000001f) {
      if (o[axis] < mn[axis] || o[axis] > mx[axis]) return false;
      continue;
    }
    float inv = 1.0f / d[axis];
    float t1 = (mn[axis] - o[axis]) * inv;
    float t2 = (mx[axis] - o[axis]) * inv;
    if (t1 > t2) std::swap(t1, t2);
    tmin = std::max(tmin, t1);
    tmax = std::min(tmax, t2);
    if (tmin > tmax) return false;
  }

  if (t_out) *t_out = tmin;
  return tmin <= max_t;
}

float ray_map(const Map& map, Vec3 origin, Vec3 dir, float max_t) {
  float best = max_t;
  for (int i = 0; i < map.box_count; ++i) {
    float t = max_t;
    if (ray_aabb(origin, dir, map_box_aabb(map.boxes[i]), best, &t) && t < best) {
      best = t;
    }
  }
  return best;
}

static bool blocked_at(const Map& map, Vec3 pos, bool crouching) {
  return map_box_overlap(map, player_aabb(pos, crouching));
}

static bool grounded_at(const Map& map, Vec3 pos, bool crouching) {
  Vec3 probe = pos;
  probe.y -= 0.05f;
  return map_box_overlap(map, player_aabb(probe, crouching));
}

static bool try_step(const Map& map, Vec3& pos, int axis, float delta, bool crouching) {
  Vec3 up = pos;
  up.y += STEP_HEIGHT;
  if (blocked_at(map, up, crouching)) return false;
  if (axis == 0) up.x += delta;
  if (axis == 2) up.z += delta;
  if (blocked_at(map, up, crouching)) return false;

  Vec3 down = up;
  int steps = 8;
  for (int i = 0; i < steps; ++i) {
    Vec3 next = down;
    next.y -= STEP_HEIGHT / static_cast<float>(steps);
    if (blocked_at(map, next, crouching)) break;
    down = next;
  }
  pos = down;
  return true;
}

MoveResult move_slide(const Map& map, Vec3 pos, Vec3 vel, bool crouching, float dt) {
  MoveResult r{pos, vel, false, false};

  float deltas[3] = {vel.x * dt, vel.y * dt, vel.z * dt};
  for (int axis : {0, 2}) {
    if (deltas[axis] == 0.0f) continue;
    int steps = std::max(1, static_cast<int>(std::ceil(std::fabs(deltas[axis]) / 0.2f)));
    float step = deltas[axis] / static_cast<float>(steps);
    for (int i = 0; i < steps; ++i) {
      Vec3 next = r.pos;
      if (axis == 0) next.x += step;
      if (axis == 2) next.z += step;
      if (!blocked_at(map, next, crouching)) {
        r.pos = next;
      } else if (!grounded_at(map, r.pos, crouching) || !try_step(map, r.pos, axis, step, crouching)) {
        if (axis == 0) r.vel.x = 0.0f;
        if (axis == 2) r.vel.z = 0.0f;
        break;
      } else {
        r.on_ground = true;
      }
      if (axis == 0 && r.vel.x == 0.0f) break;
      if (axis == 2 && r.vel.z == 0.0f) break;
    }
  }

  if (deltas[1] != 0.0f) {
    int steps = std::max(1, static_cast<int>(std::ceil(std::fabs(deltas[1]) / 0.2f)));
    float step = deltas[1] / static_cast<float>(steps);
    for (int i = 0; i < steps; ++i) {
      Vec3 next = r.pos;
      next.y += step;
      if (!blocked_at(map, next, crouching)) {
        r.pos = next;
      } else {
        if (r.vel.y < 0.0f) r.on_ground = true;
        if (r.vel.y > 0.0f) r.hit_ceiling = true;
        r.vel.y = 0.0f;
        break;
      }
    }
  }

  if (grounded_at(map, r.pos, crouching)) {
    r.on_ground = true;
    if (r.vel.y < 0.0f) r.vel.y = 0.0f;
  }

  return r;
}

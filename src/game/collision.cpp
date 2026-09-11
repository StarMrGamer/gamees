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

namespace {

// Clamped cell span covering an XZ rectangle. Callers only ever use it to walk
// buckets, so clamping (rather than rejecting) out-of-bounds queries is right:
// a query outside the grid still tests the edge cells it borders.
struct CellSpan {
  int x0, x1, z0, z1;
};

CellSpan grid_span(const MapGrid& g, float min_x, float min_z, float max_x, float max_z) {
  CellSpan s;
  s.x0 = static_cast<int>((min_x - g.org_x) * g.inv_cell_x);
  s.x1 = static_cast<int>((max_x - g.org_x) * g.inv_cell_x);
  s.z0 = static_cast<int>((min_z - g.org_z) * g.inv_cell_z);
  s.z1 = static_cast<int>((max_z - g.org_z) * g.inv_cell_z);
  if (s.x0 < 0) s.x0 = 0;
  if (s.z0 < 0) s.z0 = 0;
  if (s.x1 > MAP_GRID_DIM - 1) s.x1 = MAP_GRID_DIM - 1;
  if (s.z1 > MAP_GRID_DIM - 1) s.z1 = MAP_GRID_DIM - 1;
  return s;
}

bool grid_covers(const MapGrid& g, float x, float z, int* cx, int* cz) {
  int ix = static_cast<int>((x - g.org_x) * g.inv_cell_x);
  int iz = static_cast<int>((z - g.org_z) * g.inv_cell_z);
  if (ix < 0 || iz < 0 || ix >= MAP_GRID_DIM || iz >= MAP_GRID_DIM) return false;
  *cx = ix;
  *cz = iz;
  return true;
}

}  // namespace

// An axis-aligned box overlaps a convex brush when no single plane separates
// them. Each plane is pushed out by the box's extent along that plane's normal
// (its support radius), which turns the swept test into a point test against
// the expanded solid. The brush's bevel planes are what keep this tight near
// the brush's own edges - see MapBrush.
bool brush_box_overlap(const MapBrush& b, const Aabb& box) {
  // Bounds reject first: six compares throws out nearly every candidate the
  // grid hands us, and only survivors pay for the plane loop.
  if (box.min.x >= b.max.x || box.max.x <= b.min.x ||
      box.min.y >= b.max.y || box.max.y <= b.min.y ||
      box.min.z >= b.max.z || box.max.z <= b.min.z) {
    return false;
  }
  Vec3 center{(box.min.x + box.max.x) * 0.5f, (box.min.y + box.max.y) * 0.5f,
              (box.min.z + box.max.z) * 0.5f};
  Vec3 half{(box.max.x - box.min.x) * 0.5f, (box.max.y - box.min.y) * 0.5f,
            (box.max.z - box.min.z) * 0.5f};
  for (int i = 0; i < b.plane_count; ++i) {
    const Vec3& n = b.n[i];
    float radius = std::fabs(n.x) * half.x + std::fabs(n.y) * half.y + std::fabs(n.z) * half.z;
    if (vec3_dot(n, center) - radius > b.d[i]) return false;
  }
  return true;
}

bool map_box_overlap(const Map& map, const Aabb& box) {
  const MapGrid& g = map.grid;
  if (!g.built) {
    for (int i = 0; i < map.box_count; ++i) {
      if (aabb_overlap(box, map_box_aabb(map.boxes[i]))) return true;
    }
    for (int i = 0; i < map.brush_count; ++i) {
      if (brush_box_overlap(map.brushes[i], box)) return true;
    }
    return false;
  }
  CellSpan s = grid_span(g, box.min.x, box.min.z, box.max.x, box.max.z);
  for (int z = s.z0; z <= s.z1; ++z) {
    for (int x = s.x0; x <= s.x1; ++x) {
      int cell = z * MAP_GRID_DIM + x;
      for (int32_t e = g.box_start[cell]; e < g.box_start[cell + 1]; ++e) {
        if (aabb_overlap(box, map_box_aabb(map.boxes[g.box_entries[e]]))) return true;
      }
      for (int32_t e = g.brush_start[cell]; e < g.brush_start[cell + 1]; ++e) {
        if (brush_box_overlap(map.brushes[g.brush_entries[e]], box)) return true;
      }
    }
  }
  return false;
}

// Body of the ramp-surface test for one candidate; shared by the indexed and
// brute-force paths so they cannot drift apart.
static inline void ramp_surface_candidate(const MapRamp& r, float x, float z, bool* found,
                                          float* best) {
  if (x < r.min.x || x > r.max.x || z < r.min.z || z > r.max.z) return;
  // Evaluate the stored plane, then clamp into the ramp's own bounds so the
  // surface never runs off the end of the solid.
  float y = clampf(map_ramp_plane_y(r, x, z), r.min.y, r.max.y);
  if (!*found || y > *best) {
    *best = y;
    *found = true;
  }
}

// Runs `visit` over every ramp whose footprint covers (x, z).
template <typename Visit>
static inline void for_each_ramp_at(const Map& map, float x, float z, Visit visit) {
  const MapGrid& g = map.grid;
  if (g.built) {
    int cx = 0;
    int cz = 0;
    if (!grid_covers(g, x, z, &cx, &cz)) return;
    int cell = cz * MAP_GRID_DIM + cx;
    for (int32_t e = g.ramp_start[cell]; e < g.ramp_start[cell + 1]; ++e) {
      visit(map.ramps[g.ramp_entries[e]]);
    }
    return;
  }
  for (int i = 0; i < map.ramp_count; ++i) visit(map.ramps[i]);
}

bool map_ramp_surface(const Map& map, float x, float z, float* y_out) {
  bool found = false;
  float best = -1e30f;
  for_each_ramp_at(map, x, z, [&](const MapRamp& r) {
    ramp_surface_candidate(r, x, z, &found, &best);
  });
  if (found && y_out) *y_out = best;
  return found;
}

bool map_ramp_surface_near(const Map& map, float x, float z, float y_ref, float* y_out) {
  bool found = false;
  float best = -1e30f;
  const float reach = y_ref + STEP_HEIGHT;
  for_each_ramp_at(map, x, z, [&](const MapRamp& r) {
    bool hit = false;
    float y = 0.0f;
    ramp_surface_candidate(r, x, z, &hit, &y);
    // Terrain stacks: a hillside overhead must not mask the ground underfoot,
    // so only surfaces within stepping reach of the caller count.
    if (!hit || y > reach) return;
    if (!found || y > best) {
      best = y;
      found = true;
    }
  });
  if (found && y_out) *y_out = best;
  return found;
}

bool map_ramp_blocks(const Map& map, Vec3 pos, bool crouching) {
  const float height = crouching ? PLAYER_CROUCH_HEIGHT : PLAYER_HEIGHT;
  bool blocked = false;
  for_each_ramp_at(map, pos.x, pos.z, [&](const MapRamp& r) {
    if (blocked) return;
    bool hit = false;
    float surf = 0.0f;
    ramp_surface_candidate(r, pos.x, pos.z, &hit, &surf);
    if (!hit) return;
    // A ramp's solid body runs from its own floor up to its surface. Testing
    // only "is the surface above me" makes every ramp a column of rock down to
    // the void, which buries whatever the terrain happens to arch over.
    if (pos.y >= surf) return;
    if (pos.y + height <= r.min.y) return;
    if (surf > pos.y + STEP_HEIGHT) blocked = true;
  });
  return blocked;
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

// Ray against a convex solid: clip the parameter interval by every half-space.
// Exact, unlike the box approximation it replaces.
bool ray_brush(const MapBrush& b, Vec3 origin, Vec3 dir, float max_t, float* t_out) {
  // Same idea for rays: the cached bounds reject most brushes along the walk
  // before the per-plane interval clip runs.
  {
    float t = max_t;
    if (!ray_aabb(origin, dir, Aabb{b.min, b.max}, max_t, &t)) return false;
  }
  float tmin = 0.0f;
  float tmax = max_t;
  for (int i = 0; i < b.plane_count; ++i) {
    float denom = vec3_dot(b.n[i], dir);
    float dist = b.d[i] - vec3_dot(b.n[i], origin);
    if (std::fabs(denom) < 1e-7f) {
      if (dist < 0.0f) return false;  // parallel and outside
      continue;
    }
    float t = dist / denom;
    if (denom < 0.0f) {
      if (t > tmin) tmin = t;   // entering
    } else {
      if (t < tmax) tmax = t;   // leaving
    }
    if (tmin > tmax) return false;
  }
  if (tmin > max_t) return false;
  if (t_out) *t_out = tmin;
  return true;
}

static float ray_map_brute(const Map& map, Vec3 origin, Vec3 dir, float max_t) {
  float best = max_t;
  for (int i = 0; i < map.box_count; ++i) {
    float t = max_t;
    if (ray_aabb(origin, dir, map_box_aabb(map.boxes[i]), best, &t) && t < best) {
      best = t;
    }
  }
  for (int i = 0; i < map.ramp_count; ++i) {
    float t = max_t;
    Aabb a{map.ramps[i].min, map.ramps[i].max};
    if (ray_aabb(origin, dir, a, best, &t) && t < best) {
      best = t;
    }
  }
  for (int i = 0; i < map.brush_count; ++i) {
    float t = max_t;
    if (ray_brush(map.brushes[i], origin, dir, best, &t) && t < best) {
      best = t;
    }
  }
  return best;
}

// Below this many primitives a straight scan beats walking the grid: the whole
// geometry list fits in cache and the DDA's setup cost dominates. Measured with
// `arena --bench` - on the 45-box default map the scan is ~1.5x quicker.
constexpr int RAY_GRID_MIN_PRIMS = 128;

float ray_map(const Map& map, Vec3 origin, Vec3 dir, float max_t) {
  const MapGrid& g = map.grid;
  if (!g.built || map.box_count + map.ramp_count + map.brush_count <= RAY_GRID_MIN_PRIMS) {
    return ray_map_brute(map, origin, dir, max_t);
  }

  // Clip the ray to the grid's XZ footprint first; a shot fired from outside
  // the indexed area still has to start walking at the cell where it enters.
  const float grid_max_x = g.org_x + static_cast<float>(MAP_GRID_DIM) / g.inv_cell_x;
  const float grid_max_z = g.org_z + static_cast<float>(MAP_GRID_DIM) / g.inv_cell_z;
  float t_enter = 0.0f;
  float t_exit = max_t;
  const float o[2] = {origin.x, origin.z};
  const float d[2] = {dir.x, dir.z};
  const float lo[2] = {g.org_x, g.org_z};
  const float hi[2] = {grid_max_x, grid_max_z};
  for (int axis = 0; axis < 2; ++axis) {
    if (std::fabs(d[axis]) < 1e-6f) {
      if (o[axis] < lo[axis] || o[axis] > hi[axis]) return max_t;
      continue;
    }
    float inv = 1.0f / d[axis];
    float t0 = (lo[axis] - o[axis]) * inv;
    float t1 = (hi[axis] - o[axis]) * inv;
    if (t0 > t1) {
      float tmp = t0;
      t0 = t1;
      t1 = tmp;
    }
    if (t0 > t_enter) t_enter = t0;
    if (t1 < t_exit) t_exit = t1;
    if (t_enter > t_exit) return max_t;
  }

  // Standard Amanatides-Woo DDA across the XZ cells.
  float ex = origin.x + dir.x * t_enter;
  float ez = origin.z + dir.z * t_enter;
  int cx = static_cast<int>((ex - g.org_x) * g.inv_cell_x);
  int cz = static_cast<int>((ez - g.org_z) * g.inv_cell_z);
  if (cx < 0) cx = 0;
  if (cz < 0) cz = 0;
  if (cx > MAP_GRID_DIM - 1) cx = MAP_GRID_DIM - 1;
  if (cz > MAP_GRID_DIM - 1) cz = MAP_GRID_DIM - 1;

  const float cell_w = 1.0f / g.inv_cell_x;
  const float cell_d = 1.0f / g.inv_cell_z;
  const int step_x = dir.x > 0.0f ? 1 : (dir.x < 0.0f ? -1 : 0);
  const int step_z = dir.z > 0.0f ? 1 : (dir.z < 0.0f ? -1 : 0);

  float t_max_x = 1e30f;
  float t_delta_x = 1e30f;
  if (step_x != 0) {
    float boundary = g.org_x + static_cast<float>(cx + (step_x > 0 ? 1 : 0)) * cell_w;
    t_max_x = (boundary - origin.x) / dir.x;
    t_delta_x = cell_w / std::fabs(dir.x);
  }
  float t_max_z = 1e30f;
  float t_delta_z = 1e30f;
  if (step_z != 0) {
    float boundary = g.org_z + static_cast<float>(cz + (step_z > 0 ? 1 : 0)) * cell_d;
    t_max_z = (boundary - origin.z) / dir.z;
    t_delta_z = cell_d / std::fabs(dir.z);
  }

  float best = max_t;
  float t_cell = t_enter;
  for (;;) {
    // Everything left to visit starts beyond a hit we already have.
    if (t_cell > best) break;

    int cell = cz * MAP_GRID_DIM + cx;
    for (int32_t e = g.box_start[cell]; e < g.box_start[cell + 1]; ++e) {
      float t = max_t;
      if (ray_aabb(origin, dir, map_box_aabb(map.boxes[g.box_entries[e]]), best, &t) && t < best) {
        best = t;
      }
    }
    for (int32_t e = g.ramp_start[cell]; e < g.ramp_start[cell + 1]; ++e) {
      const MapRamp& r = map.ramps[g.ramp_entries[e]];
      float t = max_t;
      Aabb a{r.min, r.max};
      if (ray_aabb(origin, dir, a, best, &t) && t < best) best = t;
    }
    for (int32_t e = g.brush_start[cell]; e < g.brush_start[cell + 1]; ++e) {
      float t = max_t;
      if (ray_brush(map.brushes[g.brush_entries[e]], origin, dir, best, &t) && t < best) {
        best = t;
      }
    }

    if (t_max_x < t_max_z) {
      if (step_x == 0) break;
      t_cell = t_max_x;
      cx += step_x;
      if (cx < 0 || cx >= MAP_GRID_DIM) break;
      t_max_x += t_delta_x;
    } else {
      if (step_z == 0) break;
      t_cell = t_max_z;
      cz += step_z;
      if (cz < 0 || cz >= MAP_GRID_DIM) break;
      t_max_z += t_delta_z;
    }
    if (t_cell > t_exit) break;
  }
  return best;
}

// Horizontal blocking for a player at `pos`: solid boxes, plus a ramp whose
// surface is more than a step above the feet (you can't walk into its face).
static bool blocked_at(const Map& map, Vec3 pos, bool crouching) {
  if (map_box_overlap(map, player_aabb(pos, crouching))) return true;
  return map_ramp_blocks(map, pos, crouching);
}

static bool grounded_at(const Map& map, Vec3 pos, bool crouching) {
  Vec3 probe = pos;
  probe.y -= 0.05f;
  if (map_box_overlap(map, player_aabb(probe, crouching))) return true;
  float surf = 0.0f;
  if (map_ramp_surface_near(map, pos.x, pos.z, pos.y, &surf) &&
      pos.y <= surf + 0.08f && pos.y >= surf - STEP_HEIGHT) {
    return true;
  }
  return false;
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
  MoveResult r{pos, vel, {0, 0, 0}, false, false};

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
        if (axis == 0) r.wall_normal = step > 0.0f ? Vec3{-1.0f, 0.0f, 0.0f} : Vec3{1.0f, 0.0f, 0.0f};
        if (axis == 2) r.wall_normal = step > 0.0f ? Vec3{0.0f, 0.0f, -1.0f} : Vec3{0.0f, 0.0f, 1.0f};
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

  // Ramps: keep the player on the sloped surface. Up to a step of rise is
  // snapped up (walking up), and a small drop is snapped down (walking down),
  // so movement over the slope is smooth instead of stair-stepped.
  float ramp_y = 0.0f;
  if (map_ramp_surface_near(map, r.pos.x, r.pos.z, r.pos.y, &ramp_y)) {
    float rise = ramp_y - r.pos.y;
    bool snap_up = rise >= 0.0f && rise <= STEP_HEIGHT;
    bool snap_down = rise < 0.0f && rise >= -STEP_HEIGHT && r.vel.y <= 0.0f;
    if (snap_up || snap_down) {
      // The snap has to be validated like any other move. Imported maps often
      // have a ramp overlapping or tucked under a solid brush, and writing the
      // surface height in unconditionally teleports the player inside it -
      // where blocked_at() then refuses every direction and they are wedged
      // for good.
      Vec3 snapped = r.pos;
      snapped.y = ramp_y;
      if (!map_box_overlap(map, player_aabb(snapped, crouching))) {
        r.pos = snapped;
        if (r.vel.y < 0.0f) r.vel.y = 0.0f;
        r.on_ground = true;
      }
    }
  }

  return r;
}

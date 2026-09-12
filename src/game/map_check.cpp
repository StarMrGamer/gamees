#include "game/map_check.h"

#include "game/collision.h"
#include "game/tuning.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

// Ramps are solid below their sloped top surface; returns the surface height.
float ramp_surface_at(const MapRamp& r, float x, float z) {
  return std::max(r.min.y, std::min(r.max.y, map_ramp_plane_y(r, x, z)));
}

}  // namespace

MapCheckReport map_check_leaks(const Map& map, const Vec3* points, int point_count) {
  MapCheckReport rep;
  if (map.box_count <= 0 && map.ramp_count <= 0 && map.brush_count <= 0) return rep;

  Vec3 lo{1e30f, 1e30f, 1e30f};
  Vec3 hi{-1e30f, -1e30f, -1e30f};
  auto include = [&](Vec3 a, Vec3 b) {
    lo.x = std::min(lo.x, a.x); lo.y = std::min(lo.y, a.y); lo.z = std::min(lo.z, a.z);
    hi.x = std::max(hi.x, b.x); hi.y = std::max(hi.y, b.y); hi.z = std::max(hi.z, b.z);
  };
  for (int i = 0; i < map.box_count; ++i) include(map.boxes[i].min, map.boxes[i].max);
  for (int i = 0; i < map.ramp_count; ++i) include(map.ramps[i].min, map.ramps[i].max);
  for (int i = 0; i < map.brush_count; ++i) include(map.brushes[i].min, map.brushes[i].max);

  // Pick the finest voxel that keeps the grid under a memory cap.
  float voxel = 0.5f;
  for (;;) {
    double nx = (hi.x - lo.x) / voxel + 5.0;
    double ny = (hi.y - lo.y) / voxel + 5.0;
    double nz = (hi.z - lo.z) / voxel + 5.0;
    if (nx * ny * nz <= 32.0e6 || voxel >= 8.0f) break;
    voxel *= 2.0f;
  }

  const int nx = static_cast<int>((hi.x - lo.x) / voxel) + 5;
  const int ny = static_cast<int>((hi.y - lo.y) / voxel) + 5;
  const int nz = static_cast<int>((hi.z - lo.z) / voxel) + 5;
  const Vec3 org{lo.x - 2.0f * voxel, lo.y - 2.0f * voxel, lo.z - 2.0f * voxel};
  const long long cells = static_cast<long long>(nx) * ny * nz;

  std::vector<uint8_t> grid(static_cast<size_t>(cells), 0);  // 0 empty, 1 solid, 2 exterior
  auto index = [&](int x, int y, int z) -> long long {
    return (static_cast<long long>(z) * ny + y) * nx + x;
  };
  // Mark boxes.
  for (int i = 0; i < map.box_count; ++i) {
    const MapBox& b = map.boxes[i];
    int x0 = std::max(0, static_cast<int>(std::floor((b.min.x - org.x) / voxel)));
    int x1 = std::min(nx - 1, static_cast<int>(std::floor((b.max.x - org.x) / voxel)));
    int y0 = std::max(0, static_cast<int>(std::floor((b.min.y - org.y) / voxel)));
    int y1 = std::min(ny - 1, static_cast<int>(std::floor((b.max.y - org.y) / voxel)));
    int z0 = std::max(0, static_cast<int>(std::floor((b.min.z - org.z) / voxel)));
    int z1 = std::min(nz - 1, static_cast<int>(std::floor((b.max.z - org.z) / voxel)));
    for (int z = z0; z <= z1; ++z)
      for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) grid[static_cast<size_t>(index(x, y, z))] = 1;
  }

  // Mark ramps (solid up to the sloped surface).
  for (int i = 0; i < map.ramp_count; ++i) {
    const MapRamp& r = map.ramps[i];
    int x0 = std::max(0, static_cast<int>(std::floor((r.min.x - org.x) / voxel)));
    int x1 = std::min(nx - 1, static_cast<int>(std::floor((r.max.x - org.x) / voxel)));
    int z0 = std::max(0, static_cast<int>(std::floor((r.min.z - org.z) / voxel)));
    int z1 = std::min(nz - 1, static_cast<int>(std::floor((r.max.z - org.z) / voxel)));
    int y0 = std::max(0, static_cast<int>(std::floor((r.min.y - org.y) / voxel)));
    for (int z = z0; z <= z1; ++z) {
      for (int x = x0; x <= x1; ++x) {
        float cx = org.x + (static_cast<float>(x) + 0.5f) * voxel;
        float cz = org.z + (static_cast<float>(z) + 0.5f) * voxel;
        float surf = ramp_surface_at(r, cx, cz);
        int y1 = std::min(ny - 1, static_cast<int>(std::floor((surf - org.y) / voxel)));
        for (int y = y0; y <= y1; ++y) grid[static_cast<size_t>(index(x, y, z))] = 1;
      }
    }
  }

  // Mark convex brushes: a cell counts as solid when its centre is inside.
  for (int i = 0; i < map.brush_count; ++i) {
    const MapBrush& b = map.brushes[i];
    int x0 = std::max(0, static_cast<int>(std::floor((b.min.x - org.x) / voxel)));
    int x1 = std::min(nx - 1, static_cast<int>(std::floor((b.max.x - org.x) / voxel)));
    int y0 = std::max(0, static_cast<int>(std::floor((b.min.y - org.y) / voxel)));
    int y1 = std::min(ny - 1, static_cast<int>(std::floor((b.max.y - org.y) / voxel)));
    int z0 = std::max(0, static_cast<int>(std::floor((b.min.z - org.z) / voxel)));
    int z1 = std::min(nz - 1, static_cast<int>(std::floor((b.max.z - org.z) / voxel)));
    for (int z = z0; z <= z1; ++z) {
      for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
          Vec3 c{org.x + (static_cast<float>(x) + 0.5f) * voxel,
                 org.y + (static_cast<float>(y) + 0.5f) * voxel,
                 org.z + (static_cast<float>(z) + 0.5f) * voxel};
          if (map_brush_contains(b, c)) grid[static_cast<size_t>(index(x, y, z))] = 1;
        }
      }
    }
  }

  long long solid = 0;
  for (long long i = 0; i < cells; ++i) {
    if (grid[static_cast<size_t>(i)] == 1) ++solid;
  }

  rep.ran = true;
  rep.voxel_size = voxel;
  rep.grid_x = nx;
  rep.grid_y = ny;
  rep.grid_z = nz;
  rep.solid_cells = solid;
  rep.point_count = point_count;

  // Horizontal containment check, one 2D slice per distinct player height:
  // flood the empty space at that height in from the map boundary. If it
  // reaches a spawn, there is a gap in the walls a player can walk through.
  // Open-sky (vertical) connectivity is deliberately ignored.
  std::vector<int> done_heights;
  for (int p = 0; p < point_count; ++p) {
    int y = static_cast<int>(std::floor((points[p].y + 0.9f - org.y) / voxel));
    y = std::max(0, std::min(ny - 1, y));
    if (std::find(done_heights.begin(), done_heights.end(), y) != done_heights.end()) continue;
    done_heights.push_back(y);

    std::vector<uint8_t> visited(static_cast<size_t>(nx) * nz, 0);
    std::vector<std::pair<int, int>> stack2;
    auto empty_at = [&](int x, int z) {
      return grid[static_cast<size_t>(index(x, y, z))] == 0;
    };
    auto push2 = [&](int x, int z) {
      if (x < 0 || z < 0 || x >= nx || z >= nz) return;
      if (!empty_at(x, z)) return;
      size_t k = static_cast<size_t>(z) * nx + x;
      if (visited[k]) return;
      visited[k] = 1;
      stack2.push_back({x, z});
    };
    for (int z = 0; z < nz; ++z) { push2(0, z); push2(nx - 1, z); }
    for (int x = 0; x < nx; ++x) { push2(x, 0); push2(x, nz - 1); }
    while (!stack2.empty()) {
      std::pair<int, int> c = stack2.back();
      stack2.pop_back();
      push2(c.first - 1, c.second); push2(c.first + 1, c.second);
      push2(c.first, c.second - 1); push2(c.first, c.second + 1);
    }
    // How much of this slice the outside reaches; a useful scale for judging
    // how bad a leak is, and it was being reported as zero.
    for (size_t k = 0; k < visited.size(); ++k) {
      if (visited[k]) ++rep.exterior_cells;
    }

    for (int q = 0; q < point_count; ++q) {
      int qy = static_cast<int>(std::floor((points[q].y + 0.9f - org.y) / voxel));
      qy = std::max(0, std::min(ny - 1, qy));
      if (qy != y) continue;
      int x = static_cast<int>(std::floor((points[q].x - org.x) / voxel));
      int z = static_cast<int>(std::floor((points[q].z - org.z) / voxel));
      bool leak = false;
      if (x < 0 || z < 0 || x >= nx || z >= nz) {
        leak = true;
      } else if (visited[static_cast<size_t>(z) * nx + x]) {
        leak = true;
      }
      if (leak) {
        ++rep.leaked_points;
        if (!rep.leaked) rep.first_leak = points[q];
        rep.leaked = true;
      }
    }
  }
  return rep;
}

// ---------------------------------------------------------------------------
// Reachability leak check
// ---------------------------------------------------------------------------

namespace {

// Horizontal resolution of the walk. The player hull is 0.6 m wide, so half a
// metre per step samples every gap they could actually fit through.
constexpr float REACH_CELL = 0.5f;
// Vertical quantisation for the visited set. Two standing spots within this
// distance in the same column are the same place for search purposes.
constexpr float REACH_Y_QUANTUM = 0.25f;
// A plain jump clears 1.225 m (JUMP_VELOCITY^2 / 2*GRAVITY). Anything above
// that needs a double jump or a rocket, which is not what "can you walk out of
// the map" is asking about.
constexpr float REACH_CLIMB = REACH_CLIMB_HEIGHT;
// Give up on a fall after four seconds; by then the player is either resting
// or well past the void plane.
constexpr int REACH_FALL_TICKS = 4 * TICK_RATE;
constexpr int REACH_MAX_POSITIONS = 400000;

struct ReachNode {
  int ix, iz;
  float y;
};

uint64_t reach_key(int ix, int iz, float y) {
  int32_t qy = static_cast<int32_t>(std::floor(y / REACH_Y_QUANTUM));
  return (static_cast<uint64_t>(static_cast<uint32_t>(ix)) << 40) ^
         (static_cast<uint64_t>(static_cast<uint32_t>(iz)) << 20) ^
         static_cast<uint64_t>(static_cast<uint32_t>(qy) & 0xFFFFFu);
}

// Drops a player from `from` under gravity using the real collision code.
// Returns true when they come to rest (with the resting position in `out`),
// false when they fall past the void plane.
bool drop_to_rest(const Map& map, Vec3 from, Vec3* out) {
  Vec3 pos = from;
  Vec3 vel{0.0f, 0.0f, 0.0f};
  for (int i = 0; i < REACH_FALL_TICKS; ++i) {
    vel.y -= GRAVITY * TICK_DT;
    MoveResult r = move_slide(map, pos, vel, false, TICK_DT);
    pos = r.pos;
    vel = r.vel;
    if (pos.y < map.void_y) return false;
    if (r.on_ground) {
      *out = pos;
      return true;
    }
  }
  *out = pos;
  return true;  // still falling after four seconds but above the void: not a leak
}

}  // namespace

MapReachReport map_check_reachable_leaks(const Map& map) {
  return map_walk_reachable(map, nullptr);
}

MapReachReport map_walk_reachable(const Map& map, ReachVisitor* visitor) {
  MapReachReport rep;
  if ((map.box_count <= 0 && map.ramp_count <= 0) || map.spawn_count <= 0) return rep;
  rep.ran = true;

  std::vector<uint64_t> visited_list;
  std::unordered_set<uint64_t> visited;
  std::unordered_set<uint64_t> leak_columns;
  std::vector<ReachNode> queue;

  auto cell_x = [&](float x) { return static_cast<int>(std::floor(x / REACH_CELL)); };
  auto world_x = [&](int ix) { return (static_cast<float>(ix) + 0.5f) * REACH_CELL; };

  auto push = [&](int ix, int iz, float y) {
    uint64_t key = reach_key(ix, iz, y);
    if (!visited.insert(key).second) return;
    if (static_cast<int>(queue.size()) + rep.reachable_positions > REACH_MAX_POSITIONS) return;
    queue.push_back({ix, iz, y});
  };

  for (int i = 0; i < map.spawn_count; ++i) {
    Vec3 rest;
    Vec3 start = map.spawns[i];
    start.y += 0.1f;
    if (!drop_to_rest(map, start, &rest)) {
      ++rep.spawns_unsupported;
      if (rep.leaks_reported < MapReachReport::MAX_REPORTED) {
        rep.leaks[rep.leaks_reported++] = map.spawns[i];
      }
      if (rep.leak_columns == 0) rep.first_leak = map.spawns[i];
      ++rep.leak_columns;
      continue;
    }
    push(cell_x(rest.x), cell_x(rest.z), rest.y);
  }

  const int dx[4] = {1, -1, 0, 0};
  const int dz[4] = {0, 0, 1, -1};

  while (!queue.empty()) {
    ReachNode node = queue.back();
    queue.pop_back();
    ++rep.reachable_positions;
    if (rep.reachable_positions > REACH_MAX_POSITIONS) break;
    Vec3 here{world_x(node.ix), node.y, world_x(node.iz)};
    if (visitor) visitor->node(here);

    for (int d = 0; d < 4; ++d) {
      int nix = node.ix + dx[d];
      int niz = node.iz + dz[d];
      float nx = world_x(nix);
      float nz = world_x(niz);

      // Find the lowest height at or above the current one where the player
      // fits in the neighbouring column: that is where they would arrive,
      // whether by walking on the level, stepping up, or jumping.
      bool entered = false;
      float entry_y = 0.0f;
      for (float lift = 0.0f; lift <= REACH_CLIMB + 0.001f; lift += REACH_Y_QUANTUM) {
        Vec3 probe{nx, node.y + lift, nz};
        if (!map_box_overlap(map, player_aabb(probe, false))) {
          entered = true;
          entry_y = probe.y;
          break;
        }
      }
      if (!entered) continue;  // a wall, not a hole

      Vec3 rest;
      if (!drop_to_rest(map, {nx, entry_y, nz}, &rest)) {
        uint64_t col = (static_cast<uint64_t>(static_cast<uint32_t>(nix)) << 32) ^
                       static_cast<uint32_t>(niz);
        if (leak_columns.insert(col).second) {
          if (rep.leak_columns == 0) rep.first_leak = {nx, node.y, nz};
          ++rep.leak_columns;
          if (rep.leaks_reported < MapReachReport::MAX_REPORTED) {
            rep.leaks[rep.leaks_reported++] = {nx, node.y, nz};
          }
        }
        continue;
      }
      // Landing far below is legal (a drop into a pit); it is only a leak if
      // they never land at all, which drop_to_rest already told us.
      if (visitor) visitor->edge(here, rest);
      push(cell_x(rest.x), cell_x(rest.z), rest.y);
    }
  }
  return rep;
}

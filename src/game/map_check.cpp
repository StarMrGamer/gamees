#include "game/map_check.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace {

// Ramps are solid below their sloped top surface; returns the surface height.
float ramp_surface_at(const MapRamp& r, float x, float z) {
  float span_x = r.max.x - r.min.x;
  float span_z = r.max.z - r.min.z;
  float t = 0.0f;
  switch (r.dir) {
    case 0: t = span_x > 0.0f ? (x - r.min.x) / span_x : 0.0f; break;
    case 1: t = span_x > 0.0f ? (r.max.x - x) / span_x : 0.0f; break;
    case 2: t = span_z > 0.0f ? (z - r.min.z) / span_z : 0.0f; break;
    default: t = span_z > 0.0f ? (r.max.z - z) / span_z : 0.0f; break;
  }
  return r.min.y + (r.max.y - r.min.y) * std::max(0.0f, std::min(1.0f, t));
}

}  // namespace

MapCheckReport map_check_leaks(const Map& map, const Vec3* points, int point_count) {
  MapCheckReport rep;
  if (map.box_count <= 0 && map.ramp_count <= 0) return rep;

  Vec3 lo{1e30f, 1e30f, 1e30f};
  Vec3 hi{-1e30f, -1e30f, -1e30f};
  auto include = [&](Vec3 a, Vec3 b) {
    lo.x = std::min(lo.x, a.x); lo.y = std::min(lo.y, a.y); lo.z = std::min(lo.z, a.z);
    hi.x = std::max(hi.x, b.x); hi.y = std::max(hi.y, b.y); hi.z = std::max(hi.z, b.z);
  };
  for (int i = 0; i < map.box_count; ++i) include(map.boxes[i].min, map.boxes[i].max);
  for (int i = 0; i < map.ramp_count; ++i) include(map.ramps[i].min, map.ramps[i].max);

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

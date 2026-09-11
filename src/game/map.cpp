#include "game/map.h"

#include "core/log.h"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

static std::vector<std::string>& map_search_dirs() {
  static std::vector<std::string> dirs;
  return dirs;
}

void map_add_search_dir(const char* dir) {
  if (dir && dir[0]) map_search_dirs().emplace_back(dir);
}

static bool path_is_absolute(const char* path) {
  if (!path || !path[0]) return false;
  if (path[0] == '/' || path[0] == '\\') return true;
  return path[1] == ':';  // Windows drive letter
}

// ---------------------------------------------------------------------------
// Convex brushes
// ---------------------------------------------------------------------------

bool map_brush_contains(const MapBrush& b, Vec3 p) {
  for (int i = 0; i < b.plane_count; ++i) {
    if (vec3_dot(b.n[i], p) > b.d[i] + 1e-4f) return false;
  }
  return true;
}

namespace {

// The vertex where three planes meet, if they meet in exactly one point.
bool plane_triple_point(const Vec3& n0, float d0, const Vec3& n1, float d1,
                        const Vec3& n2, float d2, Vec3* out) {
  Vec3 c12 = vec3_cross(n1, n2);
  float det = vec3_dot(n0, c12);
  if (std::fabs(det) < 1e-7f) return false;
  Vec3 c20 = vec3_cross(n2, n0);
  Vec3 c01 = vec3_cross(n0, n1);
  *out = (c12 * d0 + c20 * d1 + c01 * d2) / det;
  return true;
}

}  // namespace

bool map_brush_finalize(MapBrush* brush) {
  if (!brush || brush->plane_count < 4) return false;

  // Every vertex of the solid is the meeting point of three of its planes, so
  // enumerating triples and keeping the points that satisfy the whole plane set
  // recovers the hull's corners - and with them, the bounds.
  const int count = brush->plane_count;
  bool any = false;
  Vec3 lo{0, 0, 0};
  Vec3 hi{0, 0, 0};
  for (int i = 0; i < count; ++i) {
    for (int j = i + 1; j < count; ++j) {
      for (int k = j + 1; k < count; ++k) {
        Vec3 v;
        if (!plane_triple_point(brush->n[i], brush->d[i], brush->n[j], brush->d[j],
                                brush->n[k], brush->d[k], &v)) {
          continue;
        }
        bool inside = true;
        for (int m = 0; m < count; ++m) {
          if (vec3_dot(brush->n[m], v) > brush->d[m] + 1e-3f) { inside = false; break; }
        }
        if (!inside) continue;
        if (!any) {
          lo = hi = v;
          any = true;
        } else {
          lo.x = v.x < lo.x ? v.x : lo.x;
          lo.y = v.y < lo.y ? v.y : lo.y;
          lo.z = v.z < lo.z ? v.z : lo.z;
          hi.x = v.x > hi.x ? v.x : hi.x;
          hi.y = v.y > hi.y ? v.y : hi.y;
          hi.z = v.z > hi.z ? v.z : hi.z;
        }
      }
    }
  }
  if (!any) return false;
  if (hi.x - lo.x < 0.001f || hi.y - lo.y < 0.001f || hi.z - lo.z < 0.001f) return false;
  brush->min = lo;
  brush->max = hi;

  // Add the bounding box's own planes as bevels, skipping any the brush already
  // has. Without these the swept test rounds off the brush's edges.
  const Vec3 bevel_n[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
  const float bevel_d[6] = {hi.x, -lo.x, hi.y, -lo.y, hi.z, -lo.z};
  for (int b = 0; b < 6; ++b) {
    bool have = false;
    for (int i = 0; i < brush->plane_count; ++i) {
      if (vec3_dot(brush->n[i], bevel_n[b]) > 0.999f &&
          std::fabs(brush->d[i] - bevel_d[b]) < 0.01f) {
        have = true;
        break;
      }
    }
    if (have) continue;
    if (brush->plane_count >= MAX_BRUSH_PLANES) return false;
    brush->n[brush->plane_count] = bevel_n[b];
    brush->d[brush->plane_count] = bevel_d[b];
    ++brush->plane_count;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Spatial index
// ---------------------------------------------------------------------------

namespace {

struct GridBounds {
  float min_x, min_z, max_x, max_z;
};

// Two counting passes (count, then fill) build the CSR lists without any
// dynamic allocation, which keeps Map a POD.
template <typename GetExtent>
bool build_bucket_lists(const MapGrid& g, int count, int max_entries, GetExtent extent,
                        int32_t* start, uint16_t* entries) {
  for (int i = 0; i <= MAP_GRID_CELLS; ++i) start[i] = 0;
  if (count <= 0) return true;

  auto cell_range = [&](int i, int* x0, int* x1, int* z0, int* z1) {
    float mnx, mnz, mxx, mxz;
    extent(i, &mnx, &mnz, &mxx, &mxz);
    *x0 = static_cast<int>((mnx - g.org_x) * g.inv_cell_x);
    *x1 = static_cast<int>((mxx - g.org_x) * g.inv_cell_x);
    *z0 = static_cast<int>((mnz - g.org_z) * g.inv_cell_z);
    *z1 = static_cast<int>((mxz - g.org_z) * g.inv_cell_z);
    if (*x0 < 0) *x0 = 0;
    if (*z0 < 0) *z0 = 0;
    if (*x1 > MAP_GRID_DIM - 1) *x1 = MAP_GRID_DIM - 1;
    if (*z1 > MAP_GRID_DIM - 1) *z1 = MAP_GRID_DIM - 1;
  };

  long long total = 0;
  for (int i = 0; i < count; ++i) {
    int x0, x1, z0, z1;
    cell_range(i, &x0, &x1, &z0, &z1);
    for (int z = z0; z <= z1; ++z) {
      for (int x = x0; x <= x1; ++x) ++start[z * MAP_GRID_DIM + x + 1];
    }
    total += static_cast<long long>(x1 - x0 + 1) * (z1 - z0 + 1);
    if (total > max_entries) return false;
  }
  for (int i = 0; i < MAP_GRID_CELLS; ++i) start[i + 1] += start[i];

  // `cursor` walks each bucket as it is filled; start[] is restored afterwards
  // because the fill shifts it by one bucket.
  static thread_local int32_t cursor[MAP_GRID_CELLS];
  for (int i = 0; i < MAP_GRID_CELLS; ++i) cursor[i] = start[i];
  for (int i = 0; i < count; ++i) {
    int x0, x1, z0, z1;
    cell_range(i, &x0, &x1, &z0, &z1);
    for (int z = z0; z <= z1; ++z) {
      for (int x = x0; x <= x1; ++x) entries[cursor[z * MAP_GRID_DIM + x]++] = static_cast<uint16_t>(i);
    }
  }
  return true;
}

}  // namespace

void map_build_grid(Map* out) {
  if (!out) return;
  MapGrid& g = out->grid;
  g.built = false;
  if (out->box_count <= 0 && out->ramp_count <= 0 && out->brush_count <= 0) return;

  GridBounds b{1e30f, 1e30f, -1e30f, -1e30f};
  auto grow = [&](Vec3 mn, Vec3 mx) {
    if (mn.x < b.min_x) b.min_x = mn.x;
    if (mn.z < b.min_z) b.min_z = mn.z;
    if (mx.x > b.max_x) b.max_x = mx.x;
    if (mx.z > b.max_z) b.max_z = mx.z;
  };
  for (int i = 0; i < out->box_count; ++i) grow(out->boxes[i].min, out->boxes[i].max);
  for (int i = 0; i < out->ramp_count; ++i) grow(out->ramps[i].min, out->ramps[i].max);
  for (int i = 0; i < out->brush_count; ++i) grow(out->brushes[i].min, out->brushes[i].max);

  // A hair of padding keeps a primitive that sits exactly on the far edge
  // inside the last cell instead of one past it.
  float span_x = b.max_x - b.min_x;
  float span_z = b.max_z - b.min_z;
  if (span_x < 1.0f) span_x = 1.0f;
  if (span_z < 1.0f) span_z = 1.0f;
  span_x *= 1.001f;
  span_z *= 1.001f;

  g.org_x = b.min_x;
  g.org_z = b.min_z;
  g.inv_cell_x = static_cast<float>(MAP_GRID_DIM) / span_x;
  g.inv_cell_z = static_cast<float>(MAP_GRID_DIM) / span_z;

  const Map* m = out;
  bool ok = build_bucket_lists(
      g, out->box_count, MAP_GRID_MAX_BOX_ENTRIES,
      [m](int i, float* mnx, float* mnz, float* mxx, float* mxz) {
        *mnx = m->boxes[i].min.x; *mnz = m->boxes[i].min.z;
        *mxx = m->boxes[i].max.x; *mxz = m->boxes[i].max.z;
      },
      g.box_start, g.box_entries);
  if (ok) {
    ok = build_bucket_lists(
        g, out->ramp_count, MAP_GRID_MAX_RAMP_ENTRIES,
        [m](int i, float* mnx, float* mnz, float* mxx, float* mxz) {
          *mnx = m->ramps[i].min.x; *mnz = m->ramps[i].min.z;
          *mxx = m->ramps[i].max.x; *mxz = m->ramps[i].max.z;
        },
        g.ramp_start, g.ramp_entries);
  }
  if (ok) {
    ok = build_bucket_lists(
        g, out->brush_count, MAP_GRID_MAX_BRUSH_ENTRIES,
        [m](int i, float* mnx, float* mnz, float* mxx, float* mxz) {
          *mnx = m->brushes[i].min.x; *mnz = m->brushes[i].min.z;
          *mxx = m->brushes[i].max.x; *mxz = m->brushes[i].max.z;
        },
        g.brush_start, g.brush_entries);
  }
  // A map too dense for the entry budget simply keeps the brute-force path.
  g.built = ok;
}

static void map_defaults(Map* out) {
  std::memset(out, 0, sizeof(*out));
  std::snprintf(out->name, sizeof(out->name), "arena");
  out->light_dir = vec3_normalize({-0.4f, -0.9f, -0.2f});
  out->fog_color = {0.58f, 0.66f, 0.72f};
  out->fog_density = 0.025f;
  out->sky_color = {0.54f, 0.68f, 0.82f};
  out->sky_zenith = {0.20f, 0.38f, 0.72f};
}

static bool parse_floats(std::istringstream& ss, float* vals, int count) {
  for (int i = 0; i < count; ++i) {
    if (!(ss >> vals[i])) return false;
  }
  return true;
}

bool map_parse(const char* text, Map* out) {
  if (!text || !out) return false;
  map_defaults(out);

  std::istringstream input(text);
  std::string line;
  int line_no = 0;
  // Planes follow their `brush` line, so the brush is only complete once some
  // other directive (or the end of the file) turns up.
  MapBrush* pending_brush = nullptr;
  auto close_brush = [&](Map* m) -> bool {
    if (!pending_brush) return true;
    MapBrush* b = pending_brush;
    pending_brush = nullptr;
    if (map_brush_finalize(b)) return true;
    // Unbounded or degenerate: drop it rather than carrying a broken solid.
    log_warn("map: dropping brush with %d planes that does not bound a solid", b->plane_count);
    --m->brush_count;
    return true;
  };
  while (std::getline(input, line)) {
    ++line_no;
    size_t comment = line.find('#');
    if (comment != std::string::npos) line.resize(comment);

    std::istringstream ss(line);
    std::string directive;
    if (!(ss >> directive)) continue;

    if (directive != "plane" && !close_brush(out)) return false;

    if (directive == "name") {
      std::string name;
      if (!(ss >> name)) {
        log_error("map line %d: name requires a value", line_no);
        return false;
      }
      std::snprintf(out->name, sizeof(out->name), "%s", name.c_str());
    } else if (directive == "box") {
      if (out->box_count >= MAX_MAP_BOXES) {
        log_error("map line %d: too many boxes", line_no);
        return false;
      }
      float v[9]{};
      if (!parse_floats(ss, v, 9)) {
        log_error("map line %d: box requires 9 numbers", line_no);
        return false;
      }
      MapBox& b = out->boxes[out->box_count++];
      b.min = {v[0], v[1], v[2]};
      b.max = {v[0] + v[3], v[1] + v[4], v[2] + v[5]};
      b.color = {v[6], v[7], v[8]};
    } else if (directive == "ramp") {
      if (out->ramp_count >= MAX_MAP_RAMPS) {
        log_error("map line %d: too many ramps", line_no);
        return false;
      }
      float v[6]{};
      if (!parse_floats(ss, v, 6)) {
        log_error("map line %d: ramp requires x y z w h d", line_no);
        return false;
      }
      std::string token;
      if (!(ss >> token)) {
        log_error("map line %d: ramp requires a slope", line_no);
        return false;
      }
      MapRamp& r = out->ramps[out->ramp_count++];
      r.min = {v[0], v[1], v[2]};
      r.max = {v[0] + v[3], v[1] + v[4], v[2] + v[5]};

      // Two spellings. The modern one gives the surface plane outright; the
      // legacy one names a cardinal ascent direction, which is reconstructed
      // as the corner-to-corner plane it used to imply.
      int legacy_dir = -1;
      if (token == "+x") legacy_dir = 0;
      else if (token == "-x") legacy_dir = 1;
      else if (token == "+z") legacy_dir = 2;
      else if (token == "-z") legacy_dir = 3;

      if (legacy_dir >= 0) {
        float run = (legacy_dir < 2) ? (r.max.x - r.min.x) : (r.max.z - r.min.z);
        float rise = r.max.y - r.min.y;
        if (run < 1e-6f) run = 1e-6f;
        // Surface through (low edge, min.y) and (high edge, max.y).
        Vec3 asc{legacy_dir == 0 ? 1.0f : (legacy_dir == 1 ? -1.0f : 0.0f), 0.0f,
                 legacy_dir == 2 ? 1.0f : (legacy_dir == 3 ? -1.0f : 0.0f)};
        Vec3 n = vec3_normalize({-asc.x * rise, run, -asc.z * rise});
        float lx = legacy_dir == 0 ? r.min.x : (legacy_dir == 1 ? r.max.x : r.min.x);
        float lz = legacy_dir == 2 ? r.min.z : (legacy_dir == 3 ? r.max.z : r.min.z);
        r.slope_n = n;
        r.slope_d = vec3_dot(n, Vec3{lx, r.min.y, lz});
      } else {
        float pn[3]{};
        pn[0] = std::strtof(token.c_str(), nullptr);
        if (!parse_floats(ss, pn + 1, 2)) {
          log_error("map line %d: ramp plane requires nx ny nz d", line_no);
          return false;
        }
        float pd = 0.0f;
        if (!parse_floats(ss, &pd, 1)) {
          log_error("map line %d: ramp plane requires nx ny nz d", line_no);
          return false;
        }
        Vec3 n{pn[0], pn[1], pn[2]};
        float len = vec3_length(n);
        if (len < 1e-6f) {
          log_error("map line %d: degenerate ramp plane normal", line_no);
          return false;
        }
        r.slope_n = n / len;
        r.slope_d = pd / len;
      }

      float rgb[3]{};
      if (!parse_floats(ss, rgb, 3)) {
        log_error("map line %d: ramp requires r g b", line_no);
        return false;
      }
      r.color = {rgb[0], rgb[1], rgb[2]};
    } else if (directive == "brush") {
      if (out->brush_count >= MAX_MAP_BRUSHES) {
        log_error("map line %d: too many brushes", line_no);
        return false;
      }
      float rgb[3]{};
      if (!parse_floats(ss, rgb, 3)) {
        log_error("map line %d: brush requires r g b", line_no);
        return false;
      }
      MapBrush& b = out->brushes[out->brush_count++];
      std::memset(&b, 0, sizeof(b));
      b.color = {rgb[0], rgb[1], rgb[2]};
      pending_brush = &b;
    } else if (directive == "plane") {
      if (!pending_brush) {
        log_error("map line %d: 'plane' outside a brush", line_no);
        return false;
      }
      float v[4]{};
      if (!parse_floats(ss, v, 4)) {
        log_error("map line %d: plane requires nx ny nz d", line_no);
        return false;
      }
      Vec3 n{v[0], v[1], v[2]};
      float len = vec3_length(n);
      if (len < 1e-6f) {
        log_error("map line %d: degenerate plane normal", line_no);
        return false;
      }
      if (pending_brush->plane_count >= MAX_BRUSH_PLANES) {
        log_error("map line %d: brush has too many planes (max %d)", line_no,
                  MAX_BRUSH_PLANES);
        return false;
      }
      pending_brush->n[pending_brush->plane_count] = n / len;
      pending_brush->d[pending_brush->plane_count] = v[3] / len;
      ++pending_brush->plane_count;
    } else if (directive == "spawn") {
      if (out->spawn_count >= MAX_SPAWNS) {
        log_error("map line %d: too many spawns", line_no);
        return false;
      }
      float v[4]{};
      if (!parse_floats(ss, v, 4)) {
        log_error("map line %d: spawn requires 4 numbers", line_no);
        return false;
      }
      int i = out->spawn_count++;
      out->spawns[i] = {v[0], v[1], v[2]};
      out->spawn_yaws[i] = v[3] * PI / 180.0f;
    } else if (directive == "health") {
      if (out->health_count >= MAX_SPAWNS) {
        log_error("map line %d: too many health spawns", line_no);
        return false;
      }
      float v[3]{};
      if (!parse_floats(ss, v, 3)) {
        log_error("map line %d: health requires 3 numbers", line_no);
        return false;
      }
      out->health_spawns[out->health_count++] = {v[0], v[1], v[2]};
    } else if (directive == "light") {
      float v[3]{};
      if (!parse_floats(ss, v, 3)) {
        log_error("map line %d: light requires 3 numbers", line_no);
        return false;
      }
      out->light_dir = vec3_normalize({v[0], v[1], v[2]});
    } else if (directive == "fog") {
      float v[4]{};
      if (!parse_floats(ss, v, 4)) {
        log_error("map line %d: fog requires 4 numbers", line_no);
        return false;
      }
      out->fog_color = {v[0], v[1], v[2]};
      out->fog_density = v[3];
    } else if (directive == "sky") {
      float v[3]{};
      if (!parse_floats(ss, v, 3)) {
        log_error("map line %d: sky requires 3 numbers", line_no);
        return false;
      }
      out->sky_color = {v[0], v[1], v[2]};
    } else {
      log_error("map line %d: unknown directive '%s'", line_no, directive.c_str());
      return false;
    }
  }

  if (!close_brush(out)) return false;

  if (out->spawn_count == 0) {
    log_error("map has no spawn points");
    return false;
  }

  // Void layer: well below the lowest solid geometry, so a player who falls
  // out of the map is caught and returned to a spawn.
  float min_y = 0.0f;
  bool any_geometry = false;
  for (int i = 0; i < out->box_count; ++i) {
    min_y = any_geometry ? (out->boxes[i].min.y < min_y ? out->boxes[i].min.y : min_y)
                         : out->boxes[i].min.y;
    any_geometry = true;
  }
  for (int i = 0; i < out->ramp_count; ++i) {
    min_y = any_geometry ? (out->ramps[i].min.y < min_y ? out->ramps[i].min.y : min_y)
                         : out->ramps[i].min.y;
    any_geometry = true;
  }
  for (int i = 0; i < out->brush_count; ++i) {
    min_y = any_geometry ? (out->brushes[i].min.y < min_y ? out->brushes[i].min.y : min_y)
                         : out->brushes[i].min.y;
    any_geometry = true;
  }
  out->void_y = any_geometry ? min_y - 20.0f : -50.0f;

  map_build_grid(out);
  return true;
}

// Try the path as given, then each registered search root (executable dir and
// its parents). Returns the FILE* and leaves the winning path in `resolved`.
static FILE* open_map_file(const char* path, std::string* resolved) {
  if (!path || !path[0]) return nullptr;  // fopen(nullptr) is undefined
  resolved->assign(path);
  FILE* f = std::fopen(path, "rb");
  if (f || path_is_absolute(path)) return f;

  for (const std::string& dir : map_search_dirs()) {
    if (dir.empty()) continue;
    std::string candidate = dir;
    if (candidate.back() != '/' && candidate.back() != '\\') candidate += '/';
    candidate += path;
    f = std::fopen(candidate.c_str(), "rb");
    if (f) {
      *resolved = candidate;
      return f;
    }
  }
  return nullptr;
}

bool map_load(const char* path, Map* out) {
  std::string resolved;
  FILE* f = open_map_file(path, &resolved);
  if (!f) {
    log_error("failed to open map '%s': %s", path, std::strerror(errno));
    return false;
  }
  std::fseek(f, 0, SEEK_END);
  long len = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (len < 0 || len > 1024 * 1024) {
    std::fclose(f);
    log_error("map '%s' has invalid size", resolved.c_str());
    return false;
  }
  std::string text;
  text.resize(static_cast<size_t>(len));
  if (len > 0 && std::fread(text.data(), 1, static_cast<size_t>(len), f) != static_cast<size_t>(len)) {
    std::fclose(f);
    log_error("failed to read map '%s'", resolved.c_str());
    return false;
  }
  std::fclose(f);
  return map_parse(text.c_str(), out);
}

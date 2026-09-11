#include "tools/map_import.h"

#include "core/math.h"
#include "game/map.h"
#include "game/map_check.h"
#include "game/tuning.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Tokenizer (shared by both dialects)
// ---------------------------------------------------------------------------

struct Token {
  enum Kind { Word, String, LBrace, RBrace, LParen, RParen } kind;
  std::string text;
};

static void tokenize(const std::string& s, std::vector<Token>& out) {
  size_t i = 0;
  const size_t n = s.size();
  while (i < n) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    if (std::isspace(c)) { ++i; continue; }
    if (c == '/' && i + 1 < n && s[i + 1] == '/') {
      while (i < n && s[i] != '\n') ++i;
      continue;
    }
    if (c == '{') { out.push_back({Token::LBrace, "{"}); ++i; continue; }
    if (c == '}') { out.push_back({Token::RBrace, "}"}); ++i; continue; }
    if (c == '(') { out.push_back({Token::LParen, "("}); ++i; continue; }
    if (c == ')') { out.push_back({Token::RParen, ")"}); ++i; continue; }
    if (c == '"') {
      ++i;
      std::string value;
      while (i < n) {
        char d = s[i++];
        if (d == '\\' && i < n) {
          char e = s[i++];
          value.push_back(e == 'n' ? '\n' : e);
        } else if (d == '"') {
          break;
        } else {
          value.push_back(d);
        }
      }
      out.push_back({Token::String, std::move(value)});
      continue;
    }
    size_t start = i;
    while (i < n) {
      char d = s[i];
      if (std::isspace(static_cast<unsigned char>(d)) || d == '{' || d == '}' ||
          d == '(' || d == ')' || d == '"') {
        break;
      }
      if (d == '/' && i + 1 < n && s[i + 1] == '/') break;
      ++i;
    }
    out.push_back({Token::Word, s.substr(start, i - start)});
  }
}

static bool text_is_number(const std::string& s) {
  if (s.empty()) return false;
  char* end = nullptr;
  std::strtof(s.c_str(), &end);
  return end && *end == 0;
}

// ---------------------------------------------------------------------------
// Source model
// ---------------------------------------------------------------------------

struct SourceFace {
  Vec3 p[3];
  std::string texture;
  // Compiled BSPs hand us a plane (outward normal, dist) directly instead of
  // three points. When set, p[] is unused.
  bool has_plane = false;
  Vec3 normal{};
  float dist = 0.0f;
};

struct SourceBrush {
  std::vector<SourceFace> faces;
};

struct SourceEntity {
  std::vector<std::pair<std::string, std::string>> keys;
  std::vector<SourceBrush> brushes;

  const std::string* key(const char* name) const {
    for (const auto& kv : keys) {
      if (kv.first == name) return &kv.second;
    }
    return nullptr;
  }
};

// A quad of the displacement surface, already in arena space. Displacements
// are not brushes - they are a heightfield lump the brush reader cannot see -
// so they arrive as ready-made patches.
struct SourcePatch {
  Vec3 corner[4];   // in grid order, so (0,1,2,3) walks the quad
  std::string texture;
  int disp = 0;     // which displacement this cell belongs to
  int gi = 0, gj = 0;   // its position in that displacement's grid
  int rows = 0, cols = 0;
};

struct SourceMap {
  std::vector<SourceEntity> entities;
  std::vector<SourcePatch> patches;
};

// ---------------------------------------------------------------------------
// idTech ".map" parser
// ---------------------------------------------------------------------------

static bool parse_dotmap(const std::vector<Token>& t, SourceMap* out) {
  size_t i = 0;
  while (i < t.size()) {
    if (t[i].kind != Token::LBrace) { ++i; continue; }
    ++i;
    SourceEntity entity;
    while (i < t.size() && t[i].kind != Token::RBrace) {
      if (t[i].kind == Token::LBrace) {
        ++i;
        SourceBrush brush;
        while (i < t.size() && t[i].kind != Token::RBrace &&
               t[i].kind != Token::LBrace) {
          if (t[i].kind != Token::LParen) { ++i; continue; }
          ++i;
          SourceFace face;
          bool ok = true;
          for (int p = 0; p < 3 && ok; ++p) {
            float vals[3] = {0, 0, 0};
            int got = 0;
            while (i < t.size() && t[i].kind != Token::RParen) {
              float v = 0.0f;
              if (got < 3 && text_is_number(t[i].text) && sscanf(t[i].text.c_str(), "%f", &v) == 1) {
                vals[got++] = v;
              }
              ++i;
            }
            if (i < t.size() && t[i].kind == Token::RParen) ++i;
            if (got != 3) { ok = false; break; }
            face.p[p] = {vals[0], vals[1], vals[2]};
          }
          if (ok && i < t.size() &&
              (t[i].kind == Token::Word || t[i].kind == Token::String)) {
            face.texture = t[i].text;
            ++i;
          }
          // Consume the texture's shift/scale/rotation numbers.
          while (i < t.size() && t[i].kind != Token::LParen &&
                 t[i].kind != Token::RBrace && t[i].kind != Token::LBrace) {
            ++i;
          }
          if (ok) brush.faces.push_back(std::move(face));
        }
        if (i < t.size() && t[i].kind == Token::RBrace) ++i;
        if (!brush.faces.empty()) entity.brushes.push_back(std::move(brush));
      } else if (t[i].kind == Token::Word || t[i].kind == Token::String) {
        std::string key = t[i++].text;
        if (i < t.size() && (t[i].kind == Token::Word || t[i].kind == Token::String)) {
          entity.keys.emplace_back(std::move(key), t[i++].text);
        } else {
          entity.keys.emplace_back(std::move(key), std::string());
        }
      } else {
        ++i;
      }
    }
    if (i < t.size() && t[i].kind == Token::RBrace) ++i;
    out->entities.push_back(std::move(entity));
  }
  return !out->entities.empty();
}

// ---------------------------------------------------------------------------
// VMF (KeyValues) parser
// ---------------------------------------------------------------------------

struct Kv {
  std::string key;
  std::string value;
  bool has_value = false;
  std::vector<Kv> kids;
};

static void parse_vmf_block(const std::vector<Token>& t, size_t& i, Kv* node) {
  while (i < t.size()) {
    if (t[i].kind == Token::RBrace) { ++i; return; }
    if (t[i].kind != Token::String && t[i].kind != Token::Word) { ++i; continue; }
    std::string key = t[i++].text;
    if (i < t.size() && t[i].kind == Token::LBrace) {
      ++i;
      Kv child;
      child.key = std::move(key);
      parse_vmf_block(t, i, &child);
      node->kids.push_back(std::move(child));
    } else if (i < t.size() &&
               (t[i].kind == Token::String || t[i].kind == Token::Word)) {
      Kv child;
      child.key = std::move(key);
      child.value = t[i++].text;
      child.has_value = true;
      node->kids.push_back(std::move(child));
    } else {
      Kv child;
      child.key = std::move(key);
      node->kids.push_back(std::move(child));
    }
  }
}

static const Kv* kv_child(const Kv& node, const char* key) {
  for (const Kv& k : node.kids) {
    if (k.key == key) return &k;
  }
  return nullptr;
}

static bool parse_plane_string(const std::string& s, SourceFace* face) {
  float v[9] = {0};
  int got = 0;
  const char* p = s.c_str();
  while (*p && got < 9) {
    while (*p && !std::isdigit(static_cast<unsigned char>(*p)) && *p != '-' && *p != '+') ++p;
    if (!*p) break;
    char* end = nullptr;
    float value = std::strtof(p, &end);
    if (end == p) { ++p; continue; }
    v[got++] = value;
    p = end;
  }
  if (got < 9) return false;
  for (int i = 0; i < 3; ++i) {
    face->p[i] = {v[i * 3 + 0], v[i * 3 + 1], v[i * 3 + 2]};
  }
  return true;
}

static void parse_vmf_solid(const Kv& solid, SourceBrush* brush) {
  for (const Kv& side : solid.kids) {
    if (side.key != "side") continue;
    const Kv* plane = kv_child(side, "plane");
    if (!plane || !plane->has_value) continue;
    SourceFace face;
    if (!parse_plane_string(plane->value, &face)) continue;
    const Kv* material = kv_child(side, "material");
    if (material && material->has_value) face.texture = material->value;
    brush->faces.push_back(std::move(face));
  }
}

static bool parse_vmf(const std::vector<Token>& t, SourceMap* out) {
  Kv root;
  size_t i = 0;
  parse_vmf_block(t, i, &root);

  for (const Kv& node : root.kids) {
    if (node.key == "world") {
      SourceEntity world;
      for (const Kv& k : node.kids) {
        if (k.key == "solid") {
          SourceBrush brush;
          parse_vmf_solid(k, &brush);
          if (!brush.faces.empty()) world.brushes.push_back(std::move(brush));
        } else if (k.has_value) {
          world.keys.emplace_back(k.key, k.value);
        }
      }
      out->entities.push_back(std::move(world));
    } else if (node.key == "entity") {
      SourceEntity entity;
      for (const Kv& k : node.kids) {
        if (k.key == "solid") {
          SourceBrush brush;
          parse_vmf_solid(k, &brush);
          if (!brush.faces.empty()) entity.brushes.push_back(std::move(brush));
        } else if (k.has_value) {
          entity.keys.emplace_back(k.key, k.value);
        }
      }
      out->entities.push_back(std::move(entity));
    }
  }
  return !out->entities.empty();
}

// ---------------------------------------------------------------------------
// Compiled Source/GoldSrc BSP ("VBSP")
// ---------------------------------------------------------------------------

static int32_t rd_i32(const uint8_t* p) { int32_t v; std::memcpy(&v, p, 4); return v; }
static uint16_t rd_u16(const uint8_t* p) { uint16_t v; std::memcpy(&v, p, 2); return v; }
static int16_t rd_i16(const uint8_t* p) { int16_t v; std::memcpy(&v, p, 2); return v; }
static float rd_f32(const uint8_t* p) { float v; std::memcpy(&v, p, 4); return v; }

static bool parse_entity_text(const std::vector<Token>& t, SourceMap* out) {
  size_t i = 0;
  bool found = false;
  while (i < t.size()) {
    if (t[i].kind != Token::LBrace) { ++i; continue; }
    ++i;
    SourceEntity entity;
    while (i < t.size() && t[i].kind != Token::RBrace) {
      if (t[i].kind == Token::Word || t[i].kind == Token::String) {
        std::string key = t[i++].text;
        if (i < t.size() && (t[i].kind == Token::Word || t[i].kind == Token::String)) {
          entity.keys.emplace_back(std::move(key), t[i++].text);
        } else {
          entity.keys.emplace_back(std::move(key), std::string());
        }
      } else {
        ++i;
      }
    }
    if (i < t.size() && t[i].kind == Token::RBrace) ++i;
    out->entities.push_back(std::move(entity));
    found = true;
  }
  return found;
}

// Source BSP lumps we read.
enum {
  BSP_LUMP_ENTITIES = 0,
  BSP_LUMP_PLANES = 1,
  BSP_LUMP_TEXDATA = 2,
  BSP_LUMP_TEXINFO = 6,
  BSP_LUMP_BRUSHES = 18,
  BSP_LUMP_BRUSHSIDES = 19,
  BSP_LUMP_VERTEXES = 3,
  BSP_LUMP_FACES = 7,
  BSP_LUMP_EDGES = 12,
  BSP_LUMP_SURFEDGES = 13,
  BSP_LUMP_DISPINFO = 26,
  BSP_LUMP_DISP_VERTS = 33,
  BSP_LUMP_TEXDATA_STRING_DATA = 43,
  BSP_LUMP_TEXDATA_STRING_TABLE = 44,
};

static bool parse_bsp(const uint8_t* d, size_t n, SourceMap* out, std::string* error) {
  if (n < 8 + 64 * 16 || std::memcmp(d, "VBSP", 4) != 0) return false;
  const int version = rd_i32(d + 4);
  if (version < 19 || version > 21) {
    if (error) *error = "unsupported BSP version " + std::to_string(version);
    return false;
  }

  const uint8_t* lumps = d + 8;  // 64 lump headers, 16 bytes each
  auto lump_ofs = [&](int i) { return static_cast<uint32_t>(rd_i32(lumps + i * 16)); };
  auto lump_len = [&](int i) { return static_cast<uint32_t>(rd_i32(lumps + i * 16 + 4)); };
  auto at = [&](int i, size_t off, size_t bytes, const uint8_t** p) -> bool {
    uint32_t o = lump_ofs(i), l = lump_len(i);
    if (off > l || bytes > l - off) return false;
    if (o > n || off + bytes > n - o) return false;
    *p = d + o + off;
    return true;
  };

  const int plane_count = static_cast<int>(lump_len(BSP_LUMP_PLANES) / 20);
  const int brush_count = static_cast<int>(lump_len(BSP_LUMP_BRUSHES) / 12);
  const int side_count = static_cast<int>(lump_len(BSP_LUMP_BRUSHSIDES) / 8);
  const int texinfo_count = static_cast<int>(lump_len(BSP_LUMP_TEXINFO) / 72);
  const int texdata_count = static_cast<int>(lump_len(BSP_LUMP_TEXDATA) / 32);
  const int strtab_count = static_cast<int>(lump_len(BSP_LUMP_TEXDATA_STRING_TABLE) / 4);

  auto material = [&](int texinfo) -> std::string {
    if (texinfo < 0 || texinfo >= texinfo_count) return {};
    const uint8_t* ti = nullptr;
    if (!at(BSP_LUMP_TEXINFO, static_cast<size_t>(texinfo) * 72, 72, &ti)) return {};
    int texdata = rd_i32(ti + 68);
    if (texdata < 0 || texdata >= texdata_count) return {};
    const uint8_t* td = nullptr;
    if (!at(BSP_LUMP_TEXDATA, static_cast<size_t>(texdata) * 32, 32, &td)) return {};
    int str_id = rd_i32(td + 12);
    if (str_id < 0 || str_id >= strtab_count) return {};
    const uint8_t* ent = nullptr;
    if (!at(BSP_LUMP_TEXDATA_STRING_TABLE, static_cast<size_t>(str_id) * 4, 4, &ent)) return {};
    int offset = rd_i32(ent);
    uint32_t sdata_len = lump_len(BSP_LUMP_TEXDATA_STRING_DATA);
    if (offset < 0 || static_cast<uint32_t>(offset) >= sdata_len) return {};
    const uint8_t* sdata = nullptr;
    if (!at(BSP_LUMP_TEXDATA_STRING_DATA, static_cast<size_t>(offset), 1, &sdata)) return {};
    size_t max = static_cast<size_t>(sdata_len - static_cast<uint32_t>(offset));
    size_t len = 0;
    while (len < max && sdata[len] != 0) ++len;
    return std::string(reinterpret_cast<const char*>(sdata), len);
  };

  SourceEntity world;
  world.keys.emplace_back("classname", "worldspawn");
  for (int bi = 0; bi < brush_count; ++bi) {
    const uint8_t* bp = nullptr;
    if (!at(BSP_LUMP_BRUSHES, static_cast<size_t>(bi) * 12, 12, &bp)) break;
    int firstside = rd_i32(bp);
    int numsides = rd_i32(bp + 4);
    int contents = rd_i32(bp + 8);
    // Only CONTENTS_SOLID is world geometry. Everything else that happens to
    // be a brush is either invisible in the original or not a wall at all, and
    // importing it puts blocks in the level that no player ever saw: clip
    // brushes (invisible walls the mapper used to shape movement), glass and
    // grates, area portals, origin markers, liquids. On de_dust2 that was 75
    // brushes, including 67 player-clips.
    constexpr int kSolid = 0x1;
    constexpr int kDropContents =
        0x8000 /*areaportal*/ | 0x1000000 /*origin*/ | 0x10 /*slime*/ | 0x20 /*water*/;
    if (!(contents & kSolid) || (contents & kDropContents)) continue;
    if (numsides <= 0 || numsides > 1024) continue;
    SourceBrush brush;
    for (int s = 0; s < numsides; ++s) {
      int si = firstside + s;
      if (si < 0 || si >= side_count) continue;
      const uint8_t* sp = nullptr;
      if (!at(BSP_LUMP_BRUSHSIDES, static_cast<size_t>(si) * 8, 8, &sp)) break;
      uint16_t planenum = rd_u16(sp);
      int16_t texinfo = rd_i16(sp + 2);
      if (planenum == 0xFFFF || planenum >= plane_count) continue;
      const uint8_t* pp = nullptr;
      if (!at(BSP_LUMP_PLANES, static_cast<size_t>(planenum) * 20, 20, &pp)) continue;
      SourceFace face;
      face.has_plane = true;
      face.normal = {rd_f32(pp), rd_f32(pp + 4), rd_f32(pp + 8)};
      face.dist = rd_f32(pp + 12);
      face.texture = material(texinfo);
      brush.faces.push_back(std::move(face));
    }
    // Trigger volumes (buy zones, bomb targets) are solid in the BSP but are
    // not walls, and func_door brushes are static here with no way to open
    // them. Skipping both keeps spawn rooms and doorways passable.
    bool skip = false;
    for (const SourceFace& face : brush.faces) {
      std::string t = face.texture;
      for (char& ch : t) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
      if (t.find("trigger") != std::string::npos || t.find("door") != std::string::npos) {
        skip = true;
        break;
      }
    }
    if (!skip && brush.faces.size() >= 4) world.brushes.push_back(std::move(brush));
  }
  out->entities.push_back(std::move(world));

  // ------------------------------------------------------------------
  // Displacements
  //
  // Terrain in a Source map is not a brush. It is a heightfield: a base
  // quad face plus a grid of per-vertex offsets in a separate lump. The
  // brush reader above cannot see any of it, so on de_dust2 every sandy
  // slope and open area - 69 patches, ~8000 triangles - was simply absent,
  // leaving the holes players fell through.
  // ------------------------------------------------------------------
  {
    const int disp_count = static_cast<int>(lump_len(BSP_LUMP_DISPINFO) / 176);
    const int dvert_count = static_cast<int>(lump_len(BSP_LUMP_DISP_VERTS) / 20);
    const int face_count = static_cast<int>(lump_len(BSP_LUMP_FACES) / 56);
    const int vert_count = static_cast<int>(lump_len(BSP_LUMP_VERTEXES) / 12);
    const int edge_count = static_cast<int>(lump_len(BSP_LUMP_EDGES) / 4);
    const int surfedge_count = static_cast<int>(lump_len(BSP_LUMP_SURFEDGES) / 4);

    auto vertex_at = [&](int i, Vec3* out_v) {
      const uint8_t* vp = nullptr;
      if (i < 0 || i >= vert_count) return false;
      if (!at(BSP_LUMP_VERTEXES, static_cast<size_t>(i) * 12, 12, &vp)) return false;
      *out_v = {rd_f32(vp), rd_f32(vp + 4), rd_f32(vp + 8)};
      return true;
    };
    // A surfedge is a signed index: negative means traverse the edge backwards.
    auto face_vertex = [&](int firstedge, int k, Vec3* out_v) {
      int se_index = firstedge + k;
      if (se_index < 0 || se_index >= surfedge_count) return false;
      const uint8_t* sp = nullptr;
      if (!at(BSP_LUMP_SURFEDGES, static_cast<size_t>(se_index) * 4, 4, &sp)) return false;
      int32_t se = rd_i32(sp);
      int edge_index = se >= 0 ? se : -se;
      if (edge_index < 0 || edge_index >= edge_count) return false;
      const uint8_t* ep = nullptr;
      if (!at(BSP_LUMP_EDGES, static_cast<size_t>(edge_index) * 4, 4, &ep)) return false;
      uint16_t a = rd_u16(ep);
      uint16_t b = rd_u16(ep + 2);
      return vertex_at(se >= 0 ? a : b, out_v);
    };

    for (int di = 0; di < disp_count; ++di) {
      const uint8_t* dp = nullptr;
      if (!at(BSP_LUMP_DISPINFO, static_cast<size_t>(di) * 176, 176, &dp)) break;
      Vec3 start_pos{rd_f32(dp), rd_f32(dp + 4), rd_f32(dp + 8)};
      int vert_start = rd_i32(dp + 12);
      int power = rd_i32(dp + 20);
      int map_face = static_cast<int>(rd_u16(dp + 36));
      if (power < 2 || power > 4) continue;
      if (map_face < 0 || map_face >= face_count) continue;

      const uint8_t* fp = nullptr;
      if (!at(BSP_LUMP_FACES, static_cast<size_t>(map_face) * 56, 56, &fp)) continue;
      int firstedge = rd_i32(fp + 4);
      int numedges = static_cast<int>(static_cast<int16_t>(rd_u16(fp + 8)));
      int16_t texinfo = static_cast<int16_t>(rd_u16(fp + 10));
      if (numedges != 4) continue;  // a displacement always sits on a quad

      Vec3 c[4];
      bool ok = true;
      for (int k = 0; k < 4 && ok; ++k) ok = face_vertex(firstedge, k, &c[k]);
      if (!ok) continue;

      // startPosition names which corner the grid starts from; rotate to it.
      int best = 0;
      float best_d = 1e30f;
      for (int k = 0; k < 4; ++k) {
        Vec3 diff = c[k] - start_pos;
        float d2 = vec3_dot(diff, diff);
        if (d2 < best_d) { best_d = d2; best = k; }
      }
      Vec3 q[4];
      for (int k = 0; k < 4; ++k) q[k] = c[(best + k) & 3];

      const int size = (1 << power) + 1;
      std::vector<Vec3> grid(static_cast<size_t>(size) * size);
      bool grid_ok = true;
      for (int i = 0; i < size && grid_ok; ++i) {
        float fi = static_cast<float>(i) / static_cast<float>(size - 1);
        Vec3 left = q[0] + (q[1] - q[0]) * fi;
        Vec3 right = q[3] + (q[2] - q[3]) * fi;
        for (int j = 0; j < size; ++j) {
          float fj = static_cast<float>(j) / static_cast<float>(size - 1);
          Vec3 base = left + (right - left) * fj;
          int idx = i * size + j;
          int vi = vert_start + idx;
          if (vi < 0 || vi >= dvert_count) { grid_ok = false; break; }
          const uint8_t* vp = nullptr;
          if (!at(BSP_LUMP_DISP_VERTS, static_cast<size_t>(vi) * 20, 20, &vp)) {
            grid_ok = false;
            break;
          }
          Vec3 offset{rd_f32(vp), rd_f32(vp + 4), rd_f32(vp + 8)};
          float dist = rd_f32(vp + 12);
          grid[static_cast<size_t>(idx)] = base + offset * dist;
        }
      }
      if (!grid_ok) continue;

      std::string tex = material(texinfo);
      for (int i = 0; i + 1 < size; ++i) {
        for (int j = 0; j + 1 < size; ++j) {
          SourcePatch patch;
          patch.disp = di;
          patch.gi = i;
          patch.gj = j;
          patch.rows = size - 1;
          patch.cols = size - 1;
          patch.corner[0] = grid[static_cast<size_t>(i) * size + j];
          patch.corner[1] = grid[static_cast<size_t>(i + 1) * size + j];
          patch.corner[2] = grid[static_cast<size_t>(i + 1) * size + (j + 1)];
          patch.corner[3] = grid[static_cast<size_t>(i) * size + (j + 1)];
          patch.texture = tex;
          out->patches.push_back(std::move(patch));
        }
      }
    }
  }

  // The entity lump is text with NUL separators between tokens.
  uint32_t ent_len = lump_len(BSP_LUMP_ENTITIES);
  if (ent_len > 0) {
    std::string ent(reinterpret_cast<const char*>(d + lump_ofs(BSP_LUMP_ENTITIES)), ent_len);
    for (char& c : ent) {
      if (c == '\0') c = ' ';
    }
    std::vector<Token> et;
    tokenize(ent, et);
    SourceMap embedded;
    if (parse_entity_text(et, &embedded)) {
      for (SourceEntity& e : embedded.entities) out->entities.push_back(std::move(e));
    }
  }
  return true;
}

// ---------------------------------------------------------------------------
// Geometry + conversion
// ---------------------------------------------------------------------------

// Quake/Source are Z-up; arena is Y-up. Map (x, y, z) -> (x, z, -y).
static Vec3 to_arena(Vec3 v, float scale) {
  return {v.x * scale, v.z * scale, -v.y * scale};
}

// Source yaw 0 faces +X and grows counter-clockwise; arena yaw 0 faces -Z.
static float arena_yaw_from_source(float degrees) {
  float yaw = 90.0f - degrees;
  while (yaw < 0.0f) yaw += 360.0f;
  while (yaw >= 360.0f) yaw -= 360.0f;
  return yaw;
}

struct Plane {
  Vec3 n;
  float d;  // n . x = d
};

static bool intersect3(const Plane& a, const Plane& b, const Plane& c, Vec3* out) {
  Vec3 bc = vec3_cross(b.n, c.n);
  float det = vec3_dot(a.n, bc);
  if (std::fabs(det) < 1e-6f) return false;
  Vec3 v = (bc * a.d + vec3_cross(c.n, a.n) * b.d + vec3_cross(a.n, b.n) * c.d) / det;
  *out = v;
  return true;
}

struct Half {
  Vec3 a;
  float d;  // a . x <= d
};

// AABB of the convex region cut out by a set of half-spaces: intersect every
// triple of planes and keep the points that satisfy all constraints.
static bool aabb_from_halves(const std::vector<Plane>& planes,
                             const std::vector<Half>& halves,
                             Vec3* lo, Vec3* hi) {
  *lo = Vec3{1e30f, 1e30f, 1e30f};
  *hi = Vec3{-1e30f, -1e30f, -1e30f};
  bool any = false;
  const int m = static_cast<int>(planes.size());
  for (int i = 0; i < m; ++i) {
    for (int j = i + 1; j < m; ++j) {
      for (int k = j + 1; k < m; ++k) {
        Vec3 v;
        if (!intersect3(planes[i], planes[j], planes[k], &v)) continue;
        bool inside = true;
        for (const Half& h : halves) {
          if (vec3_dot(h.a, v) - h.d > 0.01f) { inside = false; break; }
        }
        if (!inside) continue;
        any = true;
        lo->x = std::min(lo->x, v.x); hi->x = std::max(hi->x, v.x);
        lo->y = std::min(lo->y, v.y); hi->y = std::max(hi->y, v.y);
        lo->z = std::min(lo->z, v.z); hi->z = std::max(hi->z, v.z);
      }
    }
  }
  return any;
}

// Rotate a direction from source (Z-up) to arena (Y-up) space.
static Vec3 to_arena_dir(Vec3 n) {
  return {n.x, n.z, -n.y};
}

// AABB of the brush's convex hull. For text maps the plane orientation is
// unknown, so an interior point (average of face vertices) picks which side is
// solid; compiled BSPs give outward planes directly.
// Same solve as brush_aabb, but also returns the plane set oriented the way the
// engine wants it (solid is dot(n, p) <= d). Orientation is the fiddly part:
// Source usually stores outward planes but not always, so for plane-only
// brushes both readings are solved and the tighter one wins, and for
// point-defined faces each plane is oriented against an interior point.
static bool brush_solve(const SourceBrush& brush, float scale, Vec3* mn, Vec3* mx,
                        std::vector<Plane>* oriented);

static bool brush_solve_impl(const SourceBrush& brush, float scale, Vec3* mn, Vec3* mx,
                             std::vector<Plane>* oriented) {
  if (brush.faces.size() < 4) return false;

  std::vector<Plane> planes;
  std::vector<Vec3> points;
  planes.reserve(brush.faces.size());
  bool all_planes = true;
  for (const SourceFace& face : brush.faces) {
    if (face.has_plane) {
      Vec3 n = to_arena_dir(face.normal);
      float len = vec3_length(n);
      if (len < 1e-9f) { all_planes = false; continue; }
      n = n / len;
      float d = face.dist * scale / len;
      planes.push_back({n, d});
      points.push_back(n * d);
    } else {
      all_planes = false;
      Vec3 a = to_arena(face.p[0], scale);
      Vec3 b = to_arena(face.p[1], scale);
      Vec3 c = to_arena(face.p[2], scale);
      Vec3 n = vec3_cross(b - a, c - a);
      float len = vec3_length(n);
      if (len < 1e-9f) continue;
      n = n / len;
      planes.push_back({n, vec3_dot(n, a)});
      points.push_back(a);
      points.push_back(b);
      points.push_back(c);
    }
  }
  if (planes.size() < 4 || points.empty()) return false;

  auto halves_for = [&](bool outward) {
    std::vector<Half> halves;
    halves.reserve(planes.size());
    for (const Plane& p : planes) {
      halves.push_back(outward ? Half{p.n, p.d} : Half{-p.n, -p.d});
    }
    return halves;
  };

  if (all_planes) {
    // Source brushes normally store outward planes (solid is n.x <= d), but a
    // few are authored/stored inverted. Both interpretations are solved and the
    // tighter (smaller) box wins: the correct one is always a subset of the
    // flipped complement, which blows up to the surrounding region.
    Vec3 lo_a, hi_a, lo_b, hi_b;
    bool ok_a = aabb_from_halves(planes, halves_for(true), &lo_a, &hi_a);
    bool ok_b = aabb_from_halves(planes, halves_for(false), &lo_b, &hi_b);
    auto volume = [](Vec3 lo, Vec3 hi) {
      return (hi.x - lo.x) * (hi.y - lo.y) * (hi.z - lo.z);
    };
    auto take = [&](bool outward) {
      if (!oriented) return;
      oriented->clear();
      oriented->reserve(planes.size());
      for (const Plane& p : planes) {
        oriented->push_back(outward ? p : Plane{-p.n, -p.d});
      }
    };
    if (ok_a && ok_b) {
      if (volume(lo_b, hi_b) < volume(lo_a, hi_a)) { *mn = lo_b; *mx = hi_b; take(false); }
      else { *mn = lo_a; *mx = hi_a; take(true); }
      return true;
    }
    if (ok_a) { *mn = lo_a; *mx = hi_a; take(true); return true; }
    if (ok_b) { *mn = lo_b; *mx = hi_b; take(false); return true; }
  } else {
    Vec3 interior{0, 0, 0};
    for (const Vec3& p : points) interior += p;
    interior = interior / static_cast<float>(points.size());
    std::vector<Half> halves;
    halves.reserve(planes.size());
    for (const Plane& p : planes) {
      halves.push_back(vec3_dot(p.n, interior) - p.d <= 0.0f ? Half{p.n, p.d} : Half{-p.n, -p.d});
    }
    if (aabb_from_halves(planes, halves, mn, mx)) {
      if (oriented) {
        oriented->clear();
        oriented->reserve(halves.size());
        for (const Half& h : halves) oriented->push_back({h.a, h.d});
      }
      return true;
    }
    if (aabb_from_halves(planes, halves_for(true), mn, mx)) return true;
    if (aabb_from_halves(planes, halves_for(false), mn, mx)) return true;
  }

  // Last resort: bounding box of the surface points. No trustworthy plane
  // orientation came out of this, so the caller must not build a brush.
  if (oriented) oriented->clear();
  *mn = *mx = points[0];
  for (const Vec3& p : points) {
    mn->x = std::min(mn->x, p.x); mx->x = std::max(mx->x, p.x);
    mn->y = std::min(mn->y, p.y); mx->y = std::max(mx->y, p.y);
    mn->z = std::min(mn->z, p.z); mx->z = std::max(mx->z, p.z);
  }
  return true;
}

static bool brush_solve(const SourceBrush& brush, float scale, Vec3* mn, Vec3* mx,
                        std::vector<Plane>* oriented) {
  if (oriented) oriented->clear();
  return brush_solve_impl(brush, scale, mn, mx, oriented);
}

static std::string lower_ascii(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

static bool contains(const std::string& hay, const char* needle) {
  return hay.find(needle) != std::string::npos;
}

static Vec3 texture_color(const std::string& texture) {
  std::string t = lower_ascii(texture);
  if (contains(t, "water")) return {0.16f, 0.36f, 0.58f};
  if (contains(t, "lava") || contains(t, "slime")) return {0.52f, 0.16f, 0.06f};
  if (contains(t, "sky")) return {0.48f, 0.62f, 0.80f};
  if (contains(t, "brick")) return {0.54f, 0.30f, 0.22f};
  if (contains(t, "metal")) return {0.44f, 0.46f, 0.50f};
  if (contains(t, "wood")) return {0.50f, 0.36f, 0.20f};
  if (contains(t, "grass")) return {0.24f, 0.44f, 0.20f};
  if (contains(t, "concrete") || contains(t, "floor") || contains(t, "wall")) {
    return {0.40f, 0.41f, 0.43f};
  }
  uint32_t h = 2166136261u;
  for (char c : t) {
    h ^= static_cast<unsigned char>(c);
    h *= 16777619u;
  }
  return {0.25f + 0.40f * ((h & 0xff) / 255.0f),
          0.25f + 0.40f * (((h >> 8) & 0xff) / 255.0f),
          0.25f + 0.40f * (((h >> 16) & 0xff) / 255.0f)};
}

static bool classname_is_world(const std::string& cls) {
  return cls == "worldspawn" || cls == "world" || cls.empty();
}

static bool classname_is_solid_detail(const std::string& cls) {
  return cls == "func_detail" || cls == "func_static" || cls == "func_brush" ||
         cls == "func_wall" || cls == "func_detail_blocker";
}

static bool classname_is_player_spawn(const std::string& cls) {
  return contains(cls, "info_player") || contains(cls, "mp_player") ||
         contains(cls, "player_start") || cls == "info_deathmatch_start";
}

static bool classname_is_health(const std::string& cls) {
  return contains(cls, "item_health") || contains(cls, "item_medkit") ||
         contains(cls, "item_healthkit");
}

struct OutBox {
  Vec3 mn, mx;
  Vec3 color;
  float volume;
  size_t order;
};

struct OutRamp {
  Vec3 mn, mx;
  Vec3 color;
  Vec3 slope_n;
  float slope_d;
};

// A convex solid kept as its plane set, so an angled brush survives as itself
// instead of being widened to its bounding box.
struct OutBrush {
  std::vector<Plane> planes;  // oriented so solid is dot(n, p) <= d
  Vec3 color;
};

static bool parse_origin(const std::string& text, Vec3* out) {
  float v[3] = {0, 0, 0};
  if (std::sscanf(text.c_str(), "%f %f %f", &v[0], &v[1], &v[2]) != 3) return false;
  *out = {v[0], v[1], v[2]};
  return true;
}

static bool entity_yaw_degrees(const SourceEntity& e, float* out) {
  if (const std::string* angle = e.key("angle")) {
    char* end = nullptr;
    float v = std::strtof(angle->c_str(), &end);
    if (end != angle->c_str()) { *out = v; return true; }
  }
  if (const std::string* angles = e.key("angles")) {
    float pitch = 0, yaw = 0, roll = 0;
    if (std::sscanf(angles->c_str(), "%f %f %f", &pitch, &yaw, &roll) >= 2) {
      *out = yaw;
      return true;
    }
  }
  return false;
}

static void append_float(std::string* out, float v) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.3f", v);
  out->append(buf);
}

// Plane components need far more precision than box corners do. At three
// decimals a normal like (-0.707, 0, -0.707) no longer matches the bevel it is
// supposed to be, so map_brush_finalize() tries to add a duplicate, overflows
// MAX_BRUSH_PLANES and rejects the brush - which shows up as a hole in the
// level. Six decimals round-trips cleanly.
static void append_plane_float(std::string* out, float v) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.6f", v);
  out->append(buf);
}

// The value a plane component will have after being written and read back.
static float plane_round_trip(float v) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.6f", v);
  return std::strtof(buf, nullptr);
}

static bool plane_is_axis_aligned(const Plane& p) {
  int axes = (std::fabs(p.n.x) > 0.999f) + (std::fabs(p.n.y) > 0.999f) +
             (std::fabs(p.n.z) > 0.999f);
  return axes == 1;
}

// A brush that is not axis-aligned and not a simple ramp used to be emitted as
// its whole bounding box, which seals off however much of that box was open in
// the original - on de_dust2 that was 736 brushes averaging only 65% solid, so
// roughly a third of each emitted block was invented. Instead, carve the
// bounding box into cells and keep the ones the brush actually touches, then
// merge those cells back into as few boxes as possible.
//
// Cells are kept when they *intersect* the brush rather than when their centre
// is inside it. Erring toward solid matters: a missing cell is a hole in a wall
// you can see or fall through, while an extra one is at most a cell of
// over-fill, and the cell is deliberately smaller than the player.
namespace {

// Metres per carve cell, and a ceiling on cells per axis so one huge brush
// cannot eat the whole box budget. 0.25 m is under half the player width, and
// on de_dust2 it lands at ~3600 of the 4096 available boxes - going finer hits
// the cap, gets truncated, and comes out worse. Re-tune with `arena --bench`
// and the reachable-position count from `--check-map` if that budget changes.
constexpr float BRUSH_CELL_TARGET = 0.25f;
constexpr int BRUSH_CELL_MAX_AXIS = 12;

// False only when some plane puts every corner of the cell outside the brush.
bool cell_touches_brush(const std::vector<Plane>& planes, Vec3 lo, Vec3 hi) {
  for (const Plane& p : planes) {
    bool all_out = true;
    for (int c = 0; c < 8; ++c) {
      Vec3 v{(c & 1) ? hi.x : lo.x, (c & 2) ? hi.y : lo.y, (c & 4) ? hi.z : lo.z};
      if (vec3_dot(p.n, v) <= p.d + 0.001f) { all_out = false; break; }
    }
    if (all_out) return false;
  }
  return true;
}

// Drops planes that repeat one already in the set; imported brushes often
// carry duplicate faces, and every slot saved is one the bevels can use.
void dedupe_planes(std::vector<Plane>* planes) {
  std::vector<Plane> out;
  out.reserve(planes->size());
  for (const Plane& p : *planes) {
    bool dup = false;
    for (const Plane& q : out) {
      if (vec3_dot(p.n, q.n) > 0.9995f && std::fabs(p.d - q.d) < 0.002f) { dup = true; break; }
    }
    if (!dup) out.push_back(p);
  }
  planes->swap(out);
}

int axis_divisions(float extent) {
  int n = static_cast<int>(std::ceil(extent / BRUSH_CELL_TARGET));
  if (n < 1) n = 1;
  if (n > BRUSH_CELL_MAX_AXIS) n = BRUSH_CELL_MAX_AXIS;
  return n;
}

}  // namespace

// Append the arena geometry for one source brush. Axis-aligned brushes become a
// single box; a brush with one upward-sloping face becomes a ramp primitive so
// it reads and plays as a real slope.
static void append_brush_boxes(const SourceBrush& brush, float scale, bool subdivide,
                               std::vector<OutBox>& boxes, std::vector<OutRamp>& ramps,
                               std::vector<OutBrush>& out_brushes,
                               MapImportResult* result) {
  std::vector<Plane> planes;
  planes.reserve(brush.faces.size());
  for (const SourceFace& f : brush.faces) {
    if (f.has_plane) {
      Vec3 n = to_arena_dir(f.normal);
      float len = vec3_length(n);
      if (len < 1e-9f) continue;
      planes.push_back({n / len, f.dist * scale / len});
    } else {
      Vec3 a = to_arena(f.p[0], scale);
      Vec3 b = to_arena(f.p[1], scale);
      Vec3 c = to_arena(f.p[2], scale);
      Vec3 n = vec3_cross(b - a, c - a);
      float len = vec3_length(n);
      if (len < 1e-9f) continue;
      planes.push_back({n / len, vec3_dot(n / len, a)});
    }
  }

  std::string tex = brush.faces.empty() ? std::string() : lower_ascii(brush.faces[0].texture);
  bool sky_brush = contains(tex, "toolsskybox") || contains(tex, "skybox");
  Vec3 color = texture_color(tex);
  auto add_box = [&](Vec3 mn, Vec3 mx) {
    float sx = mx.x - mn.x, sy = mx.y - mn.y, sz = mx.z - mn.z;
    if (sx < 0.02f || sy < 0.02f || sz < 0.02f) {
      if (result) ++result->boxes_dropped;
      return;
    }
    boxes.push_back({mn, mx, color, sx * sy * sz, boxes.size()});
  };

  Vec3 mn, mx;
  std::vector<Plane> oriented;
  if (!brush_solve(brush, scale, &mn, &mx, &oriented)) {
    if (result) ++result->boxes_dropped;
    return;
  }

  // Drop horizontal sky caps so the level is open to the sky. Vertical sky
  // walls stay, keeping the play space sealed.
  if (sky_brush) {
    float horiz = std::max(mx.x - mn.x, mx.z - mn.z);
    if (mx.y - mn.y < 0.25f * horiz) {
      if (result) ++result->boxes_dropped;
      return;
    }
  }

  const Plane* slope = nullptr;
  int non_axis = 0;
  for (const Plane& p : planes) {
    if (plane_is_axis_aligned(p)) continue;
    ++non_axis;
    if (!slope || std::fabs(p.n.y) > std::fabs(slope->n.y)) slope = &p;
  }

  if (slope && non_axis <= 2 && std::fabs(slope->n.y) > 0.2f &&
      std::fabs(slope->n.y) < 0.98f && mx.y - mn.y > 0.05f &&
      ramps.size() < static_cast<size_t>(MAX_MAP_RAMPS)) {
    // Store the surface plane as it actually is. Snapping it to a cardinal
    // direction and re-deriving it from the bounding box is what used to put
    // slopes in the wrong place.
    Vec3 sn = slope->n;
    float sd = slope->d;
    if (sn.y < 0.0f) { sn = -sn; sd = -sd; }
    ramps.push_back({mn, mx, color, sn, sd});
    if (result) ++result->brushes_ramped;
    return;
  }

  if (non_axis == 0) {
    if (result) ++result->brushes_exact;
    add_box(mn, mx);
    return;
  }

  {
    // How much of the bounding box the brush really fills. A brush that nearly
    // fills it is not worth carving up.
    const int N = 8;
    int inside = 0;
    for (int i = 0; i < N; ++i) {
      for (int j = 0; j < N; ++j) {
        for (int k = 0; k < N; ++k) {
          Vec3 sp{mn.x + (mx.x - mn.x) * (i + 0.5f) / N,
                  mn.y + (mx.y - mn.y) * (j + 0.5f) / N,
                  mn.z + (mx.z - mn.z) * (k + 0.5f) / N};
          bool in = true;
          for (const Plane& p : planes) {
            if (vec3_dot(p.n, sp) > p.d + 0.001f) { in = false; break; }
          }
          if (in) ++inside;
        }
      }
    }
    float fill = static_cast<float>(inside) / static_cast<float>(N * N * N);
    if (result) {
      ++result->brushes_approximated;
      result->approx_fill += fill;
    }

    // Keep the real shape whenever the engine can hold it. This is the whole
    // point: a diagonal wall stays a diagonal wall instead of becoming the
    // solid block of its bounding box.
    if (!oriented.empty() && out_brushes.size() < static_cast<size_t>(MAX_MAP_BRUSHES)) {
      std::vector<Plane> keep = oriented;
      dedupe_planes(&keep);
      // Validate with the engine's own routine rather than a lookalike. A brush
      // the parser would reject becomes a hole in the level, so "would this
      // load?" has to be answered by the exact code that will load it.
      if (static_cast<int>(keep.size()) >= 4 &&
          static_cast<int>(keep.size()) <= MAX_BRUSH_PLANES) {
        // Quantise first, then normalise the way the parser will, so the probe
        // sees byte-for-byte what the loader will see.
        for (Plane& pl : keep) {
          pl.n = {plane_round_trip(pl.n.x), plane_round_trip(pl.n.y), plane_round_trip(pl.n.z)};
          pl.d = plane_round_trip(pl.d);
        }
        MapBrush probe{};
        probe.plane_count = static_cast<uint8_t>(keep.size());
        for (size_t i = 0; i < keep.size(); ++i) {
          float len = vec3_length(keep[i].n);
          if (len < 1e-6f) { probe.plane_count = 0; break; }
          probe.n[i] = keep[i].n / len;
          probe.d[i] = keep[i].d / len;
        }
        if (probe.plane_count >= 4 && map_brush_finalize(&probe)) {
          out_brushes.push_back({std::move(keep), color});
          if (result) ++result->brushes_kept;
          return;
        }
      }
    }

    if (fill > 0.9f || !subdivide) {
      add_box(mn, mx);
      return;
    }

    const int nx = axis_divisions(mx.x - mn.x);
    const int ny = axis_divisions(mx.y - mn.y);
    const int nz = axis_divisions(mx.z - mn.z);
    const float cx = (mx.x - mn.x) / static_cast<float>(nx);
    const float cy = (mx.y - mn.y) / static_cast<float>(ny);
    const float cz = (mx.z - mn.z) / static_cast<float>(nz);

    std::vector<uint8_t> keep(static_cast<size_t>(nx) * ny * nz, 0);
    auto at = [&](int i, int j, int k) -> uint8_t& {
      return keep[(static_cast<size_t>(k) * ny + j) * nx + i];
    };
    int kept = 0;
    for (int i = 0; i < nx; ++i) {
      for (int j = 0; j < ny; ++j) {
        for (int k = 0; k < nz; ++k) {
          Vec3 lo{mn.x + cx * i, mn.y + cy * j, mn.z + cz * k};
          Vec3 hi{lo.x + cx, lo.y + cy, lo.z + cz};
          if (cell_touches_brush(planes, lo, hi)) { at(i, j, k) = 1; ++kept; }
        }
      }
    }
    if (kept == 0) {
      add_box(mn, mx);  // should not happen, but never emit nothing
      return;
    }

    // Greedy merge of kept cells into maximal boxes: grow along x, then y,
    // then z, so a slab comes out as one box rather than a cloud of cells.
    for (int k = 0; k < nz; ++k) {
      for (int j = 0; j < ny; ++j) {
        for (int i = 0; i < nx; ++i) {
          if (!at(i, j, k)) continue;
          int i1 = i;
          while (i1 + 1 < nx && at(i1 + 1, j, k)) ++i1;
          int j1 = j;
          for (;;) {
            if (j1 + 1 >= ny) break;
            bool full = true;
            for (int ii = i; ii <= i1; ++ii) if (!at(ii, j1 + 1, k)) { full = false; break; }
            if (!full) break;
            ++j1;
          }
          int k1 = k;
          for (;;) {
            if (k1 + 1 >= nz) break;
            bool full = true;
            for (int ii = i; ii <= i1 && full; ++ii)
              for (int jj = j; jj <= j1; ++jj)
                if (!at(ii, jj, k1 + 1)) { full = false; break; }
            if (!full) break;
            ++k1;
          }
          for (int ii = i; ii <= i1; ++ii)
            for (int jj = j; jj <= j1; ++jj)
              for (int kk = k; kk <= k1; ++kk) at(ii, jj, kk) = 0;
          add_box({mn.x + cx * i, mn.y + cy * j, mn.z + cz * k},
                  {mn.x + cx * (i1 + 1), mn.y + cy * (j1 + 1), mn.z + cz * (k1 + 1)});
        }
      }
    }
  }
}

// True if a player standing at `s` (feet on the ground) would be inside solid
// geometry - a box, or a ramp whose surface is more than a step above the feet.
// Surface height of a ramp at (x, z), clamped into its bounds - the same rule
// the engine applies in map_ramp_surface().
static float ramp_surface_of(const OutRamp& r, float x, float z) {
  if (r.slope_n.y > -1e-6f && r.slope_n.y < 1e-6f) return r.mn.y;
  float y = (r.slope_d - r.slope_n.x * x - r.slope_n.z * z) / r.slope_n.y;
  return std::max(r.mn.y, std::min(r.mx.y, y));
}

// Turn displacement patches into walkable slopes.
//
// Only MapRamp gives a surface the player can walk up; a convex brush would be
// a wall. So each patch becomes a ramp: its bounds, plus the best-fit plane of
// its four corners. Patches are emitted per grid cell, which is far too many to
// keep one-to-one, so neighbouring cells that share a plane are merged first -
// flat ground collapses to a handful of ramps and only genuinely curved terrain
// costs many.
// `world_mn`/`world_mx` bound the playable area (the solid brushes). Terrain
// outside it is the 3D skybox: Source builds distant scenery as a separate
// miniature region stored in the same lumps, and importing it drops a copy of
// the horizon into the level hundreds of metres from anything playable.
static void patches_to_ramps(const std::vector<SourcePatch>& patches, float scale,
                             float thickness, Vec3 world_mn, Vec3 world_mx,
                             std::vector<OutRamp>& ramps, MapImportResult* result) {
  const float kMargin = 8.0f;
  struct Cell {
    Vec3 mn, mx;
    Vec3 n;
    float d;
    Vec3 color;
    bool valid = false;
    bool used = false;
  };

  // Cells only ever merge with their neighbours inside the same displacement.
  // Merging by "overlaps the bounding rectangle" instead fuses patches from
  // opposite ends of the level into one enormous slab.
  size_t at = 0;
  while (at < patches.size()) {
    const int disp = patches[at].disp;
    const int rows = patches[at].rows;
    const int cols = patches[at].cols;
    size_t end_idx = at;
    while (end_idx < patches.size() && patches[end_idx].disp == disp) ++end_idx;
    if (rows <= 0 || cols <= 0) { at = end_idx; continue; }

    std::vector<Cell> grid(static_cast<size_t>(rows) * cols);
    for (size_t k = at; k < end_idx; ++k) {
      const SourcePatch& p = patches[k];
      if (p.gi >= rows || p.gj >= cols) continue;
      Vec3 v[4];
      for (int c = 0; c < 4; ++c) v[c] = to_arena(p.corner[c], scale);
      Vec3 n1 = vec3_cross(v[1] - v[0], v[2] - v[0]);
      Vec3 n2 = vec3_cross(v[2] - v[0], v[3] - v[0]);
      Vec3 n = n1 + n2;
      float len = vec3_length(n);
      if (len < 1e-9f) continue;
      n = n / len;
      if (n.y < 0.0f) n = -n;
      // Near-vertical cells are cliff faces, not floor. A ramp cannot express
      // one, and forcing it makes a surface that shoots off to infinity.
      if (n.y < 0.05f) continue;

      Cell& c = grid[static_cast<size_t>(p.gi) * cols + p.gj];
      c.mn = c.mx = v[0];
      for (int q = 1; q < 4; ++q) {
        c.mn.x = std::min(c.mn.x, v[q].x); c.mx.x = std::max(c.mx.x, v[q].x);
        c.mn.y = std::min(c.mn.y, v[q].y); c.mx.y = std::max(c.mx.y, v[q].y);
        c.mn.z = std::min(c.mn.z, v[q].z); c.mx.z = std::max(c.mx.z, v[q].z);
      }
      // Reject anything outside the playable bounds (3D skybox scenery).
      Vec3 mid{(c.mn.x + c.mx.x) * 0.5f, (c.mn.y + c.mx.y) * 0.5f, (c.mn.z + c.mx.z) * 0.5f};
      if (mid.x < world_mn.x - kMargin || mid.x > world_mx.x + kMargin ||
          mid.y < world_mn.y - kMargin || mid.y > world_mx.y + kMargin ||
          mid.z < world_mn.z - kMargin || mid.z > world_mx.z + kMargin) {
        if (result) ++result->patches_outside;
        continue;
      }
      c.mn.y -= thickness;
      c.n = n;
      c.d = vec3_dot(n, (v[0] + v[1] + v[2] + v[3]) / 4.0f);
      c.color = texture_color(lower_ascii(p.texture));
      c.valid = true;
    }

    // Terrain is a smooth heightfield, so neighbouring cells rarely share a
    // plane exactly. A few degrees of slack merges the gentle areas into big
    // ramps while leaving genuinely curved ground at cell resolution.
    auto same_plane = [](const Cell& a, const Cell& b) {
      return a.valid && b.valid && vec3_dot(a.n, b.n) > 0.9986f &&
             std::fabs(a.d - b.d) < 0.06f;
    };

    for (int i = 0; i < rows; ++i) {
      for (int j = 0; j < cols; ++j) {
        Cell& seed = grid[static_cast<size_t>(i) * cols + j];
        if (!seed.valid || seed.used) continue;
        int j1 = j;
        while (j1 + 1 < cols) {
          Cell& nxt = grid[static_cast<size_t>(i) * cols + (j1 + 1)];
          if (nxt.used || !same_plane(seed, nxt)) break;
          ++j1;
        }
        int i1 = i;
        for (;;) {
          if (i1 + 1 >= rows) break;
          bool row_ok = true;
          for (int jj = j; jj <= j1; ++jj) {
            Cell& nxt = grid[static_cast<size_t>(i1 + 1) * cols + jj];
            if (nxt.used || !same_plane(seed, nxt)) { row_ok = false; break; }
          }
          if (!row_ok) break;
          ++i1;
        }
        Vec3 mn = seed.mn;
        Vec3 mx = seed.mx;
        for (int ii = i; ii <= i1; ++ii) {
          for (int jj = j; jj <= j1; ++jj) {
            Cell& c = grid[static_cast<size_t>(ii) * cols + jj];
            c.used = true;
            mn.x = std::min(mn.x, c.mn.x); mx.x = std::max(mx.x, c.mx.x);
            mn.y = std::min(mn.y, c.mn.y); mx.y = std::max(mx.y, c.mx.y);
            mn.z = std::min(mn.z, c.mn.z); mx.z = std::max(mx.z, c.mx.z);
          }
        }
        if (ramps.size() >= static_cast<size_t>(MAX_MAP_RAMPS)) {
          if (result) ++result->patches_dropped;
          continue;
        }
        ramps.push_back({mn, mx, seed.color, seed.n, seed.d});
        if (result) ++result->patches_kept;
      }
    }
    at = end_idx;
  }
}

static bool spawn_blocked(const std::vector<OutBox>& boxes, const std::vector<OutRamp>& ramps,
                          Vec3 s) {
  const float hw = PLAYER_HALF_W;
  const float h = PLAYER_HEIGHT;
  float x0 = s.x - hw, x1 = s.x + hw;
  float y0 = s.y, y1 = s.y + h;
  float z0 = s.z - hw, z1 = s.z + hw;
  for (const OutBox& b : boxes) {
    if (x0 < b.mx.x && x1 > b.mn.x && y0 < b.mx.y && y1 > b.mn.y && z0 < b.mx.z && z1 > b.mn.z) {
      return true;
    }
  }
  for (const OutRamp& r : ramps) {
    if (s.x < r.mn.x || s.x > r.mx.x || s.z < r.mn.z || s.z > r.mx.z) continue;
    if (ramp_surface_of(r, s.x, s.z) > s.y + STEP_HEIGHT) return true;
  }
  return false;
}

static int uf_find(std::vector<int>& parent, int a) {
  while (parent[a] != a) {
    parent[a] = parent[parent[a]];
    a = parent[a];
  }
  return a;
}

// Drop geometry that is not connected to the main playable mass - the 3D
// skybox and other floating decoration sit far from the level and would show up
// as an unused second map. Components near the main mass are kept, so a real
// area that our box approximation happened to split is not thrown away.
static void filter_disconnected(std::vector<OutBox>& boxes, MapImportResult* result) {
  const int n = static_cast<int>(boxes.size());
  if (n < 3) return;
  const float tol = 0.06f;

  std::vector<int> parent(n);
  for (int i = 0; i < n; ++i) parent[i] = i;
  for (int i = 0; i < n; ++i) {
    const OutBox& a = boxes[i];
    for (int j = i + 1; j < n; ++j) {
      const OutBox& b = boxes[j];
      if (a.mn.x <= b.mx.x + tol && a.mx.x + tol >= b.mn.x &&
          a.mn.y <= b.mx.y + tol && a.mx.y + tol >= b.mn.y &&
          a.mn.z <= b.mx.z + tol && a.mx.z + tol >= b.mn.z) {
        int ra = uf_find(parent, i);
        int rb = uf_find(parent, j);
        if (ra != rb) parent[ra] = rb;
      }
    }
  }

  std::vector<int> count(n, 0);
  for (int i = 0; i < n; ++i) ++count[uf_find(parent, i)];
  int primary = 0;
  int best = -1;
  for (int i = 0; i < n; ++i) {
    if (count[i] > best) { best = count[i]; primary = i; }
  }

  // Per-component bounding boxes.
  std::vector<Vec3> cmin(n, Vec3{1e30f, 1e30f, 1e30f});
  std::vector<Vec3> cmax(n, Vec3{-1e30f, -1e30f, -1e30f});
  for (int i = 0; i < n; ++i) {
    int r = uf_find(parent, i);
    cmin[r].x = std::min(cmin[r].x, boxes[i].mn.x); cmax[r].x = std::max(cmax[r].x, boxes[i].mx.x);
    cmin[r].y = std::min(cmin[r].y, boxes[i].mn.y); cmax[r].y = std::max(cmax[r].y, boxes[i].mx.y);
    cmin[r].z = std::min(cmin[r].z, boxes[i].mn.z); cmax[r].z = std::max(cmax[r].z, boxes[i].mx.z);
  }
  Vec3 pmin = cmin[primary];
  Vec3 pmax = cmax[primary];

  constexpr float keep_dist = 1.5f;
  constexpr float encl_tol = 1.0f;
  auto encloses_primary = [&](int r) {
    return cmin[r].x <= pmin.x + encl_tol && cmin[r].y <= pmin.y + encl_tol &&
           cmin[r].z <= pmin.z + encl_tol && cmax[r].x >= pmax.x - encl_tol &&
           cmax[r].y >= pmax.y - encl_tol && cmax[r].z >= pmax.z - encl_tol;
  };
  auto distance_to_primary = [&](const OutBox& b) {
    float dx = std::max(0.0f, std::max(pmin.x - b.mx.x, b.mn.x - pmax.x));
    float dy = std::max(0.0f, std::max(pmin.y - b.mx.y, b.mn.y - pmax.y));
    float dz = std::max(0.0f, std::max(pmin.z - b.mx.z, b.mn.z - pmax.z));
    return std::sqrt(dx * dx + dy * dy + dz * dz);
  };

  std::vector<OutBox> kept;
  kept.reserve(boxes.size());
  int dropped = 0;
  for (int i = 0; i < n; ++i) {
    int r = uf_find(parent, i);
    // Keep the main mass, anything around it, and any surrounding shell (the
    // skybox) so the play space stays sealed; drop the far 3D skybox.
    if (r == primary || encloses_primary(r) || distance_to_primary(boxes[i]) <= keep_dist) {
      kept.push_back(boxes[i]);
    } else {
      ++dropped;
    }
  }
  if (dropped > 0) {
    if (result) result->boxes_dropped += dropped;
    boxes = std::move(kept);
  }
}

}  // namespace

std::string map_import_sanitize_name(const char* name) {
  std::string out;
  if (!name) return "imported";
  for (const char* p = name; *p; ++p) {
    unsigned char c = static_cast<unsigned char>(*p);
    if (std::isalnum(c)) out.push_back(static_cast<char>(std::tolower(c)));
    else if (c == '-' || c == '_' || c == ' ') out.push_back('_');
  }
  if (out.empty()) out = "imported";
  return out;
}

// Basename without directory or extension, e.g. "maps/q3dm17.map" -> "q3dm17".
static std::string source_stem(const char* source_name) {
  std::string stem = source_name ? source_name : "imported";
  size_t slash = stem.find_last_of("/\\");
  if (slash != std::string::npos) stem = stem.substr(slash + 1);
  size_t dot = stem.find_last_of('.');
  if (dot != std::string::npos) stem = stem.substr(0, dot);
  return map_import_sanitize_name(stem.c_str());
}

// Shared tail: turn a parsed source model into arena map text.
static bool convert_source(const SourceMap& source, const std::string& name,
                           const MapImportOptions& options,
                           std::string* out_text, MapImportResult* result,
                           std::string* error) {
  const int max_boxes = options.max_boxes > 0 ? options.max_boxes : MAX_MAP_BOXES;

  std::vector<OutBox> boxes;
  std::vector<OutRamp> ramps;
  std::vector<OutBrush> out_brushes;
  std::vector<Vec3> spawns;
  std::vector<float> spawn_yaws;
  std::vector<Vec3> health;

  for (const SourceEntity& entity : source.entities) {
    const std::string* cls_key = entity.key("classname");
    std::string cls = cls_key ? lower_ascii(*cls_key) : std::string();

    bool take_brushes = classname_is_world(cls) ||
                        (options.include_detail && classname_is_solid_detail(cls));
    if (take_brushes) {
      for (const SourceBrush& brush : entity.brushes) {
        if (result) ++result->brushes_seen;
        append_brush_boxes(brush, options.scale, options.subdivide, boxes, ramps, out_brushes, result);
      }
    }

    if (classname_is_player_spawn(cls)) {
      const std::string* origin = entity.key("origin");
      Vec3 o;
      if (origin && parse_origin(*origin, &o) && spawns.size() < 64u) {
        float yaw_deg = 0.0f;
        entity_yaw_degrees(entity, &yaw_deg);
        spawns.push_back(to_arena(o, options.scale));
        spawn_yaws.push_back(arena_yaw_from_source(yaw_deg));
      }
    } else if (classname_is_health(cls)) {
      const std::string* origin = entity.key("origin");
      Vec3 o;
      if (origin && parse_origin(*origin, &o) && health.size() < static_cast<size_t>(MAX_SPAWNS)) {
        health.push_back(to_arena(o, options.scale));
      }
    }
  }

  // Drop spawns embedded in the imported geometry, then cap to the engine
  // limit. Source maps often have more spawns than we can use.
  {
    std::vector<Vec3> ok;
    std::vector<float> ok_yaws;
    for (size_t i = 0; i < spawns.size(); ++i) {
      if (!spawn_blocked(boxes, ramps, spawns[i])) {
        ok.push_back(spawns[i]);
        ok_yaws.push_back(spawn_yaws[i]);
      }
    }
    if (!ok.empty()) {
      spawns = std::move(ok);
      spawn_yaws = std::move(ok_yaws);
    }
    if (spawns.size() > static_cast<size_t>(MAX_SPAWNS)) {
      spawns.resize(MAX_SPAWNS);
      spawn_yaws.resize(MAX_SPAWNS);
    }
  }

  if (boxes.empty()) {
    if (error) *error = "no solid geometry found in source map";
    return false;
  }
  if (spawns.empty()) {
    if (error) *error = "no player spawn entity found in source map";
    return false;
  }

  filter_disconnected(boxes, result);

  if (static_cast<int>(boxes.size()) > max_boxes) {
    std::vector<size_t> order(boxes.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
      return boxes[a].volume > boxes[b].volume;
    });
    std::vector<char> keep(boxes.size(), 0);
    for (int i = 0; i < max_boxes; ++i) keep[order[static_cast<size_t>(i)]] = 1;
    std::vector<OutBox> kept;
    kept.reserve(static_cast<size_t>(max_boxes));
    for (size_t i = 0; i < boxes.size(); ++i) {
      if (keep[i]) kept.push_back(boxes[i]);
    }
    if (result) {
      result->boxes_truncated = static_cast<int>(boxes.size()) - max_boxes;
      result->truncated = true;
    }
    boxes = std::move(kept);
  }

  // The playable area is whatever the *pruned* boxes span. Only boxes go
  // through filter_disconnected(), so they are the one geometry list already
  // free of 3D-skybox leftovers; deriving the bounds from ramps or brushes
  // instead lets a single stray skybox slope stretch them across the map.
  Vec3 wmn{1e30f, 1e30f, 1e30f};
  Vec3 wmx{-1e30f, -1e30f, -1e30f};
  for (const OutBox& b : boxes) {
    wmn.x = std::min(wmn.x, b.mn.x); wmn.y = std::min(wmn.y, b.mn.y);
    wmn.z = std::min(wmn.z, b.mn.z);
    wmx.x = std::max(wmx.x, b.mx.x); wmx.y = std::max(wmx.y, b.mx.y);
    wmx.z = std::max(wmx.z, b.mx.z);
  }
  const bool have_bounds = wmn.x <= wmx.x;

  // Ramps and brushes are not pruned, so drop the skybox ones the same way.
  if (have_bounds) {
    const float kOutside = 8.0f;
    auto outside = [&](Vec3 mn, Vec3 mx) {
      Vec3 mid{(mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f, (mn.z + mx.z) * 0.5f};
      return mid.x < wmn.x - kOutside || mid.x > wmx.x + kOutside ||
             mid.y < wmn.y - kOutside || mid.y > wmx.y + kOutside ||
             mid.z < wmn.z - kOutside || mid.z > wmx.z + kOutside;
    };
    size_t before = ramps.size() + out_brushes.size();
    std::vector<OutRamp> kept_ramps;
    kept_ramps.reserve(ramps.size());
    for (const OutRamp& r : ramps) {
      if (!outside(r.mn, r.mx)) kept_ramps.push_back(r);
    }
    ramps.swap(kept_ramps);
    std::vector<OutBrush> kept_brushes;
    kept_brushes.reserve(out_brushes.size());
    for (const OutBrush& b : out_brushes) {
      Vec3 bmn{1e30f, 1e30f, 1e30f};
      Vec3 bmx{-1e30f, -1e30f, -1e30f};
      MapBrush probe{};
      probe.plane_count = static_cast<uint8_t>(b.planes.size());
      for (size_t i = 0; i < b.planes.size(); ++i) {
        probe.n[i] = b.planes[i].n;
        probe.d[i] = b.planes[i].d;
      }
      if (map_brush_finalize(&probe)) { bmn = probe.min; bmx = probe.max; }
      if (bmn.x > bmx.x || !outside(bmn, bmx)) kept_brushes.push_back(b);
    }
    out_brushes.swap(kept_brushes);
    if (result) {
      result->skybox_dropped =
          static_cast<int>(before - (ramps.size() + out_brushes.size()));
    }
  }

  // Displacement terrain, converted after the brushes so it shares the ramp
  // budget with them.
  if (result) result->patches_seen = static_cast<int>(source.patches.size());
  if (have_bounds) {
    patches_to_ramps(source.patches, options.scale, 0.6f, wmn, wmx, ramps, result);
  }

  // Leak check: does the outside connect to any spawn? Build a temporary Map
  // and flood-fill the empty space from beyond the geometry.
  {
    Map check_map{};
    check_map.box_count = static_cast<int>(std::min<size_t>(boxes.size(), MAX_MAP_BOXES));
    for (int i = 0; i < check_map.box_count; ++i) {
      check_map.boxes[i].min = boxes[i].mn;
      check_map.boxes[i].max = boxes[i].mx;
      check_map.boxes[i].color = boxes[i].color;
    }
    check_map.ramp_count = static_cast<int>(std::min<size_t>(ramps.size(), MAX_MAP_RAMPS));
    for (int i = 0; i < check_map.ramp_count; ++i) {
      check_map.ramps[i].min = ramps[i].mn;
      check_map.ramps[i].max = ramps[i].mx;
      check_map.ramps[i].color = ramps[i].color;
      check_map.ramps[i].slope_n = ramps[i].slope_n;
      check_map.ramps[i].slope_d = ramps[i].slope_d;
    }
    check_map.brush_count =
        static_cast<int>(std::min<size_t>(out_brushes.size(), MAX_MAP_BRUSHES));
    for (int i = 0; i < check_map.brush_count; ++i) {
      MapBrush& cb = check_map.brushes[i];
      cb.plane_count = static_cast<uint8_t>(out_brushes[i].planes.size());
      for (int k = 0; k < cb.plane_count; ++k) {
        cb.n[k] = out_brushes[i].planes[k].n;
        cb.d[k] = out_brushes[i].planes[k].d;
      }
      cb.color = out_brushes[i].color;
      map_brush_finalize(&cb);
    }
    MapCheckReport rep = map_check_leaks(check_map, spawns.data(), static_cast<int>(spawns.size()));
    if (result && rep.ran && rep.leaked) {
      result->leaks_to_void = true;
      result->leaked_spawns = rep.leaked_points;
      result->first_leak = rep.first_leak;
    }
  }

  if (result) result->name = name;

  std::string out;
  out.reserve(boxes.size() * 48 + 256);
  out += "name " + name + "\n";
  out += "sky 0.50 0.63 0.77\n";
  out += "fog 0.56 0.64 0.73 0.013\n";
  out += "light -0.42 -0.86 -0.28\n";
  out += "# Generated by the arena map importer from a source map.\n\n";
  out += "# ---- solids ----\n";
  for (const OutBox& b : boxes) {
    out += "box ";
    append_float(&out, b.mn.x); out += ' ';
    append_float(&out, b.mn.y); out += ' ';
    append_float(&out, b.mn.z); out += ' ';
    append_float(&out, b.mx.x - b.mn.x); out += ' ';
    append_float(&out, b.mx.y - b.mn.y); out += ' ';
    append_float(&out, b.mx.z - b.mn.z); out += ' ';
    append_float(&out, b.color.x); out += ' ';
    append_float(&out, b.color.y); out += ' ';
    append_float(&out, b.color.z);
    out += '\n';
  }
  if (!out_brushes.empty()) {
    out += "\n# ---- convex brushes (angled solids, kept as plane sets) ----\n";
    for (const OutBrush& b : out_brushes) {
      out += "brush ";
      append_float(&out, b.color.x); out += ' ';
      append_float(&out, b.color.y); out += ' ';
      append_float(&out, b.color.z);
      out += '\n';
      for (const Plane& p : b.planes) {
        out += "plane ";
        append_plane_float(&out, p.n.x); out += ' ';
        append_plane_float(&out, p.n.y); out += ' ';
        append_plane_float(&out, p.n.z); out += ' ';
        append_plane_float(&out, p.d);
        out += '\n';
      }
    }
  }
  if (!ramps.empty()) {
    out += "\n# ---- ramps (walkable slopes: bounds, surface plane, colour) ----\n";
    for (const OutRamp& r : ramps) {
      out += "ramp ";
      append_float(&out, r.mn.x); out += ' ';
      append_float(&out, r.mn.y); out += ' ';
      append_float(&out, r.mn.z); out += ' ';
      append_float(&out, r.mx.x - r.mn.x); out += ' ';
      append_float(&out, r.mx.y - r.mn.y); out += ' ';
      append_float(&out, r.mx.z - r.mn.z); out += ' ';
      append_plane_float(&out, r.slope_n.x); out += ' ';
      append_plane_float(&out, r.slope_n.y); out += ' ';
      append_plane_float(&out, r.slope_n.z); out += ' ';
      append_plane_float(&out, r.slope_d); out += ' ';
      append_float(&out, r.color.x); out += ' ';
      append_float(&out, r.color.y); out += ' ';
      append_float(&out, r.color.z);
      out += '\n';
    }
  }
  out += "\n# ---- spawns ----\n";
  for (size_t i = 0; i < spawns.size(); ++i) {
    out += "spawn ";
    append_float(&out, spawns[i].x); out += ' ';
    append_float(&out, spawns[i].y); out += ' ';
    append_float(&out, spawns[i].z); out += ' ';
    append_float(&out, spawn_yaws[i]);
    out += '\n';
  }
  if (!health.empty()) {
    out += "\n# ---- health ----\n";
    for (const Vec3& h : health) {
      out += "health ";
      append_float(&out, h.x); out += ' ';
      append_float(&out, h.y); out += ' ';
      append_float(&out, h.z);
      out += '\n';
    }
  }

  *out_text = std::move(out);
  if (result) {
    result->boxes_written = static_cast<int>(boxes.size());
    result->ramps_written = static_cast<int>(ramps.size());
    result->spawns = static_cast<int>(spawns.size());
    result->health = static_cast<int>(health.size());
    // approx_fill accumulated a per-brush fraction above; turn it into a mean.
    result->approx_fill = result->brushes_approximated > 0
        ? result->approx_fill / static_cast<float>(result->brushes_approximated)
        : 1.0f;
  }
  return true;
}

bool map_source_to_arena(const char* text, const char* source_name,
                         const MapImportOptions& options,
                         std::string* out_text, MapImportResult* result,
                         std::string* error) {
  if (!text || !out_text) {
    if (error) *error = "no input";
    return false;
  }
  if (result) *result = MapImportResult{};

  std::vector<Token> tokens;
  tokenize(text, tokens);

  bool looks_vmf = false;
  {
    std::string name = source_name ? lower_ascii(source_name) : std::string();
    looks_vmf = name.size() >= 4 && name.compare(name.size() - 4, 4, ".vmf") == 0;
  }
  if (!looks_vmf) {
    for (const Token& tok : tokens) {
      if (tok.kind == Token::String &&
          (tok.text == "versioninfo" || tok.text == "editorversion")) {
        looks_vmf = true;
        break;
      }
    }
  }

  SourceMap source;
  bool parsed = looks_vmf ? parse_vmf(tokens, &source) : parse_dotmap(tokens, &source);
  if (!parsed || source.entities.empty()) {
    if (error) *error = "could not parse source map (no entities found)";
    return false;
  }
  return convert_source(source, source_stem(source_name), options, out_text, result, error);
}

bool map_import_bytes_to_arena(const void* data, size_t size, const char* source_name,
                               const MapImportOptions& options,
                               std::string* out_text, MapImportResult* result,
                               std::string* error) {
  if (!data || !out_text) {
    if (error) *error = "no input";
    return false;
  }
  const uint8_t* bytes = static_cast<const uint8_t*>(data);
  if (size >= 4 && std::memcmp(bytes, "VBSP", 4) == 0) {
    if (result) *result = MapImportResult{};
    SourceMap source;
    if (!parse_bsp(bytes, size, &source, error)) {
      if (error && error->empty()) *error = "could not parse BSP";
      return false;
    }
    return convert_source(source, source_stem(source_name), options, out_text, result, error);
  }
  std::string text(reinterpret_cast<const char*>(data), size);
  return map_source_to_arena(text.c_str(), source_name, options, out_text, result, error);
}

bool map_import_file(const char* in_path, const char* out_path,
                     const MapImportOptions& options, MapImportResult* result,
                     std::string* error) {
  if (!in_path) {
    if (error) *error = "missing input path";
    return false;
  }
  std::ifstream in(in_path, std::ios::binary);
  if (!in) {
    if (error) *error = std::string("failed to open source map '") + in_path + "'";
    return false;
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  std::string data = buffer.str();

  std::string arena_text;
  MapImportResult local_result;
  if (!map_import_bytes_to_arena(data.data(), data.size(), in_path, options, &arena_text,
                                 &local_result, error)) {
    return false;
  }

  std::string out;
  if (out_path && out_path[0]) {
    out = out_path;
  } else {
    out = "maps/" + local_result.name + ".txt";
  }
  std::ofstream file(out, std::ios::binary);
  if (!file) {
    if (error) *error = std::string("failed to open output '") + out + "'";
    return false;
  }
  file << arena_text;
  if (!file) {
    if (error) *error = std::string("failed to write output '") + out + "'";
    return false;
  }
  if (result) *result = local_result;
  return true;
}

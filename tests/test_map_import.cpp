#include "test_harness.h"

#include "game/collision.h"
#include "game/map.h"
#include "game/map_check.h"
#include "tools/map_import.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

// ---------------------------------------------------------------------------
// Fixture builders (a 1-unit == 1 m scale is used so the expected numbers are
// the source coordinates after the Z-up -> Y-up conversion).
// ---------------------------------------------------------------------------

static std::string map_face(float ax, float ay, float az, float bx, float by, float bz,
                            float cx, float cy, float cz, const char* tex) {
  char buf[192];
  std::snprintf(buf, sizeof(buf), "( %g %g %g ) ( %g %g %g ) ( %g %g %g ) %s 0 0 0 1 1\n",
                ax, ay, az, bx, by, bz, cx, cy, cz, tex);
  return buf;
}

// Six faces of the axis-aligned box [x0,x1]x[y0,y1]x[z0,z1] in source coords.
static std::string map_box(float x0, float y0, float z0, float x1, float y1, float z1,
                           const char* tex) {
  std::string s = "{\n";
  s += map_face(x0, y0, z0, x0, y1, z0, x0, y1, z1, tex);
  s += map_face(x1, y0, z0, x1, y1, z0, x1, y1, z1, tex);
  s += map_face(x0, y0, z0, x1, y0, z0, x1, y0, z1, tex);
  s += map_face(x0, y1, z0, x1, y1, z0, x1, y1, z1, tex);
  s += map_face(x0, y0, z0, x1, y0, z0, x1, y1, z0, tex);
  s += map_face(x0, y0, z1, x1, y0, z1, x1, y1, z1, tex);
  s += "}\n";
  return s;
}

static std::string dotmap_world(const std::string& brushes) {
  return "// tiny test map\n"
         "{\n\"classname\" \"worldspawn\"\n" + brushes + "}\n";
}

static std::string dotmap_spawn(float x, float y, float z, float angle) {
  char buf[160];
  std::snprintf(buf, sizeof(buf),
                "{\n\"classname\" \"info_player_deathmatch\"\n"
                "\"origin\" \"%g %g %g\"\n\"angle\" \"%g\"\n}\n",
                x, y, z, angle);
  return buf;
}

static std::string dotmap_health(float x, float y, float z) {
  char buf[160];
  std::snprintf(buf, sizeof(buf),
                "{\n\"classname\" \"item_health\"\n\"origin\" \"%g %g %g\"\n}\n", x, y, z);
  return buf;
}

static std::string plane_str(float ax, float ay, float az, float bx, float by, float bz,
                             float cx, float cy, float cz) {
  char buf[192];
  std::snprintf(buf, sizeof(buf), "(%g %g %g) (%g %g %g) (%g %g %g)",
                ax, ay, az, bx, by, bz, cx, cy, cz);
  return buf;
}

static std::string vmf_side(const std::string& plane, const char* mat) {
  return "side\n{\n\"plane\" \"" + plane + "\"\n\"material\" \"" + mat + "\"\n}\n";
}

static std::string vmf_world(float x0, float y0, float z0, float x1, float y1, float z1) {
  std::string s = "world\n{\n\"classname\" \"worldspawn\"\nsolid\n{\n";
  s += vmf_side(plane_str(x0, y0, z0, x0, y1, z0, x0, y1, z1), "BRICK/BRICKWALL");
  s += vmf_side(plane_str(x1, y0, z0, x1, y1, z0, x1, y1, z1), "BRICK/BRICKWALL");
  s += vmf_side(plane_str(x0, y0, z0, x1, y0, z0, x1, y0, z1), "CONCRETE/CONCRETEWALL");
  s += vmf_side(plane_str(x0, y1, z0, x1, y1, z0, x1, y1, z1), "CONCRETE/CONCRETEWALL");
  s += vmf_side(plane_str(x0, y0, z0, x1, y0, z0, x1, y1, z0), "FLOOR/TILE");
  s += vmf_side(plane_str(x0, y0, z1, x1, y0, z1, x1, y1, z1), "FLOOR/TILE");
  s += "}\n}\n";
  return s;
}

static std::string vmf_entity(const char* cls, const char* origin, const char* angles) {
  std::string s = "entity\n{\n";
  s += std::string("\"classname\" \"") + cls + "\"\n";
  s += std::string("\"origin\" \"") + origin + "\"\n";
  if (angles) s += std::string("\"angles\" \"") + angles + "\"\n";
  s += "}\n";
  return s;
}

// ---------------------------------------------------------------------------

TEST(dotmap_imports_brushes_spawns_and_health) {
  std::string src = dotmap_world(map_box(-64, -64, -16, 64, 64, 0, "BRICK/BRICKWALL"));
  src += dotmap_spawn(0, 0, 24, 0.0f);
  src += dotmap_health(32, 0, 8);

  MapImportOptions options;
  options.scale = 1.0f;
  std::string out;
  MapImportResult result;
  std::string error;
  CHECK(map_source_to_arena(src.c_str(), "tiny.map", options, &out, &result, &error));
  CHECK(result.name == "tiny");
  CHECK_EQ_INT(result.boxes_written, 1);
  CHECK_EQ_INT(result.spawns, 1);
  CHECK_EQ_INT(result.health, 1);
  CHECK(!result.truncated);

  Map map{};
  CHECK(map_parse(out.c_str(), &map));
  CHECK_EQ_INT(map.box_count, 1);
  // Quake Z-up (x,y,z) -> arena Y-up (x, z, -y).
  CHECK_NEAR(map.boxes[0].min.x, -64.0f, 0.01f);
  CHECK_NEAR(map.boxes[0].min.y, -16.0f, 0.01f);
  CHECK_NEAR(map.boxes[0].min.z, -64.0f, 0.01f);
  CHECK_NEAR(map.boxes[0].max.x, 64.0f, 0.01f);
  CHECK_NEAR(map.boxes[0].max.y, 0.0f, 0.01f);
  CHECK_NEAR(map.boxes[0].max.z, 64.0f, 0.01f);

  CHECK_EQ_INT(map.spawn_count, 1);
  CHECK_NEAR(map.spawns[0].x, 0.0f, 0.01f);
  CHECK_NEAR(map.spawns[0].y, 24.0f, 0.01f);
  CHECK_NEAR(map.spawns[0].z, 0.0f, 0.01f);
  // Source angle 0 (facing +X) becomes arena yaw 90 degrees.
  CHECK_NEAR(map.spawn_yaws[0], PI * 0.5f, 0.001f);

  CHECK_EQ_INT(map.health_count, 1);
  CHECK_NEAR(map.health_spawns[0].x, 32.0f, 0.01f);
  CHECK_NEAR(map.health_spawns[0].y, 8.0f, 0.01f);
}

TEST(vmf_imports_world_and_entity) {
  std::string src = "versioninfo\n{\n\"editorversion\" \"400\"\n}\n";
  src += vmf_world(-64, -64, -16, 64, 64, 0);
  src += vmf_entity("info_player_start", "0 0 24", "0 90 0");
  src += vmf_entity("item_healthkit", "32 0 8", nullptr);

  MapImportOptions options;
  options.scale = 1.0f;
  std::string out;
  MapImportResult result;
  std::string error;
  CHECK(map_source_to_arena(src.c_str(), "deck.vmf", options, &out, &result, &error));
  CHECK(result.name == "deck");
  CHECK_EQ_INT(result.boxes_written, 1);
  CHECK_EQ_INT(result.spawns, 1);
  CHECK_EQ_INT(result.health, 1);

  Map map{};
  CHECK(map_parse(out.c_str(), &map));
  CHECK_EQ_INT(map.box_count, 1);
  CHECK_NEAR(map.boxes[0].min.y, -16.0f, 0.01f);
  // angles "0 90 0" -> yaw 90 -> arena 90 - 90 = 0.
  CHECK_NEAR(map.spawn_yaws[0], 0.0f, 0.001f);
}

TEST(map_import_truncates_to_box_limit) {
  std::string brushes = map_box(-64, -64, -16, 64, 64, 0, "TEX");
  brushes += map_box(-8, -8, 0, 8, 8, 8, "TEX");
  brushes += map_box(-4, -4, 8, 4, 4, 12, "TEX");
  std::string src = dotmap_world(brushes) + dotmap_spawn(0, 0, 24, 0.0f);

  MapImportOptions options;
  options.scale = 1.0f;
  options.max_boxes = 1;
  std::string out;
  MapImportResult result;
  std::string error;
  CHECK(map_source_to_arena(src.c_str(), "big.map", options, &out, &result, &error));
  CHECK(result.truncated);
  CHECK_EQ_INT(result.boxes_written, 1);
  CHECK_EQ_INT(result.boxes_truncated, 2);
  // The largest brush (the floor) must survive.
  Map map{};
  CHECK(map_parse(out.c_str(), &map));
  CHECK_EQ_INT(map.box_count, 1);
  CHECK_NEAR(map.boxes[0].max.x - map.boxes[0].min.x, 128.0f, 0.01f);
}

TEST(map_import_requires_a_spawn) {
  std::string src = dotmap_world(map_box(-64, -64, -16, 64, 64, 0, "TEX"));
  MapImportOptions options;
  options.scale = 1.0f;
  std::string out;
  MapImportResult result;
  std::string error;
  CHECK(!map_source_to_arena(src.c_str(), "nospawn.map", options, &out, &result, &error));
  CHECK(!error.empty());
}

static void put_i32(std::string* s, int v) {
  char b[4];
  std::memcpy(b, &v, 4);
  s->append(b, 4);
}

static void put_f32(std::string* s, float v) {
  char b[4];
  std::memcpy(b, &v, 4);
  s->append(b, 4);
}

// A minimal but valid Source BSP v20: one box brush plus a spawn and health.
static std::string build_test_bsp() {
  const int kHeaderLumps = 8 + 64 * 16;
  std::string bsp = "VBSP";
  put_i32(&bsp, 20);
  bsp.append(64 * 16, '\0');
  auto set_lump = [&](int lump, int ofs, int len) {
    size_t p = 8 + static_cast<size_t>(lump) * 16;
    std::memcpy(&bsp[p], &ofs, 4);
    std::memcpy(&bsp[p + 4], &len, 4);
  };
  auto add = [&](int lump, const std::string& data) {
    set_lump(lump, static_cast<int>(bsp.size()), static_cast<int>(data.size()));
    bsp += data;
  };
  (void)kHeaderLumps;

  std::string entities =
      "{\n\"classname\" \"worldspawn\"\n}\n"
      "{\n\"classname\" \"info_player_start\"\n\"origin\" \"0 0 24\"\n\"angles\" \"0 0 0\"\n}\n"
      "{\n\"classname\" \"item_healthkit\"\n\"origin\" \"32 0 8\"\n}\n";
  add(0, entities);

  // Six outward planes of the box x,y in [-64,64], z in [-16,0].
  std::string planes;
  auto plane = [&](float nx, float ny, float nz, float d) {
    put_f32(&planes, nx); put_f32(&planes, ny); put_f32(&planes, nz); put_f32(&planes, d);
    put_i32(&planes, 0);
  };
  plane(1, 0, 0, 64);
  plane(-1, 0, 0, 64);
  plane(0, 1, 0, 64);
  plane(0, -1, 0, 64);
  plane(0, 0, 1, 0);
  plane(0, 0, -1, 16);
  add(1, planes);

  std::string texdata(32, '\0');
  int name_id = 0;
  std::memcpy(&texdata[12], &name_id, 4);
  add(2, texdata);

  std::string texinfo(72, '\0');
  int texdata_index = 0;
  std::memcpy(&texinfo[68], &texdata_index, 4);
  add(6, texinfo);

  std::string brushes;
  put_i32(&brushes, 0);  // firstside
  put_i32(&brushes, 6);  // numsides
  put_i32(&brushes, 1);  // contents = SOLID
  add(18, brushes);

  std::string sides;
  for (int i = 0; i < 6; ++i) {
    uint16_t planenum = static_cast<uint16_t>(i);
    int16_t texinfo16 = 0;
    int16_t dispinfo = -1;
    int16_t bevel = 0;
    char b[8];
    std::memcpy(b, &planenum, 2);
    std::memcpy(b + 2, &texinfo16, 2);
    std::memcpy(b + 4, &dispinfo, 2);
    std::memcpy(b + 6, &bevel, 2);
    sides.append(b, 8);
  }
  add(19, sides);

  add(43, std::string("\0TEST", 6));
  std::string strtab;
  put_i32(&strtab, 1);
  add(44, strtab);
  return bsp;
}

TEST(bsp_imports_world_and_entity_lump) {
  std::string bsp = build_test_bsp();
  MapImportOptions options;
  options.scale = 1.0f;
  std::string out;
  MapImportResult result;
  std::string error;
  CHECK(map_import_bytes_to_arena(bsp.data(), bsp.size(), "dust.bsp", options, &out, &result,
                                  &error));
  CHECK(result.name == "dust");
  CHECK_EQ_INT(result.boxes_written, 1);
  CHECK_EQ_INT(result.spawns, 1);
  CHECK_EQ_INT(result.health, 1);

  Map map{};
  CHECK(map_parse(out.c_str(), &map));
  CHECK_EQ_INT(map.box_count, 1);
  CHECK_NEAR(map.boxes[0].min.x, -64.0f, 0.01f);
  CHECK_NEAR(map.boxes[0].min.y, -16.0f, 0.01f);
  CHECK_NEAR(map.boxes[0].max.z, 64.0f, 0.01f);
  CHECK_EQ_INT(map.spawn_count, 1);
  CHECK_NEAR(map.spawns[0].y, 24.0f, 0.01f);
  CHECK_EQ_INT(map.health_count, 1);
}

TEST(map_parse_loads_ramps) {
  const char* text =
      "name ramptest\n"
      "ramp -10 0 -10 20 5 20 +x 0.5 0.5 0.5\n"
      "spawn 0 0 0 0\n";
  Map map{};
  CHECK(map_parse(text, &map));
  CHECK_EQ_INT(map.ramp_count, 1);
  float y = -1.0f;
  CHECK(map_ramp_surface(map, -10.0f, 0.0f, &y));
  CHECK_NEAR(y, 0.0f, 0.001f);
  CHECK(map_ramp_surface(map, 0.0f, 0.0f, &y));
  CHECK_NEAR(y, 2.5f, 0.001f);
  CHECK(map_ramp_surface(map, 10.0f, 0.0f, &y));
  CHECK_NEAR(y, 5.0f, 0.001f);
  CHECK(!map_ramp_surface(map, 30.0f, 0.0f, &y));
}

TEST(map_check_detects_void_leak) {
  Map open{};
  open.boxes[open.box_count++] = {{-5, -1, -5}, {5, 0, 5}, {1, 1, 1}};  // floor only
  open.spawns[0] = {0, 0, 0};
  open.spawn_count = 1;
  MapCheckReport r1 = map_check_leaks(open, open.spawns, 1);
  CHECK(r1.ran);
  CHECK(r1.leaked);

  Map sealed{};
  sealed.boxes[sealed.box_count++] = {{-5, -1, -5}, {5, 0, 5}, {1, 1, 1}};   // floor
  sealed.boxes[sealed.box_count++] = {{-5, 3, -5}, {5, 4, 5}, {1, 1, 1}};    // ceiling
  sealed.boxes[sealed.box_count++] = {{-5, 0, -6}, {5, 4, -5}, {1, 1, 1}};   // -z wall
  sealed.boxes[sealed.box_count++] = {{-5, 0, 5}, {5, 4, 6}, {1, 1, 1}};     // +z wall
  sealed.boxes[sealed.box_count++] = {{-6, 0, -5}, {-5, 4, 5}, {1, 1, 1}};   // -x wall
  sealed.boxes[sealed.box_count++] = {{5, 0, -5}, {6, 4, 5}, {1, 1, 1}};     // +x wall
  sealed.spawns[0] = {0, 0, 0};
  sealed.spawn_count = 1;
  MapCheckReport r2 = map_check_leaks(sealed, sealed.spawns, 1);
  CHECK(r2.ran);
  CHECK(!r2.leaked);
}

TEST(map_import_sanitizes_names) {
  CHECK(map_import_sanitize_name("q3dm17") == "q3dm17");
  CHECK(map_import_sanitize_name("The Longest Yard") == "the_longest_yard");
  CHECK(map_import_sanitize_name("") == "imported");
}

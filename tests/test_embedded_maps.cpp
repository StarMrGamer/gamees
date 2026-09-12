#include "game/embedded_maps.h"
#include "game/map.h"
#include "test_harness.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

// Reads a map from disk, or returns empty if it is not there. The tests run
// from the build directory in some setups and the repo root in others, so try
// both rather than assuming.
std::string read_map_file(const char* name) {
  const char* roots[] = {"maps/", "../maps/", "../../maps/"};
  for (const char* root : roots) {
    std::string path = std::string(root) + name;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) continue;
    std::string text;
    char buf[4096];
    size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
    std::fclose(f);
    return text;
  }
  return std::string();
}

}  // namespace

// The executable is meant to be the only file needed to run the game, so every
// map in maps/ has to be inside it. This is the test that catches somebody
// adding a map and shipping a binary that cannot load it.
TEST(every_map_is_embedded_in_the_binary) {
  CHECK(embedded_map_count() > 0);
  bool saw_arena = false;
  bool saw_dust2 = false;
  for (int i = 0; i < embedded_map_count(); ++i) {
    const char* name = embedded_map_name(i);
    CHECK(name != nullptr);
    if (std::strcmp(name, "arena.txt") == 0) saw_arena = true;
    if (std::strcmp(name, "de_dust2.txt") == 0) saw_dust2 = true;
  }
  CHECK(saw_arena);
  CHECK(saw_dust2);
}

// Byte-for-byte: an embedded map that has drifted from the file is worse than
// one that is missing, because it loads and plays subtly differently.
TEST(embedded_maps_match_the_files_on_disk) {
  int checked = 0;
  for (int i = 0; i < embedded_map_count(); ++i) {
    const char* name = embedded_map_name(i);
    std::string on_disk = read_map_file(name);
    if (on_disk.empty()) continue;  // running somewhere without the repo beside us
    int size = 0;
    const char* baked = embedded_map_text(name, &size);
    CHECK(baked != nullptr);
    CHECK_EQ_INT(size, static_cast<int>(on_disk.size()));
    CHECK(std::memcmp(baked, on_disk.data(), on_disk.size()) == 0);
    ++checked;
  }
  CHECK(checked > 0);  // guard against a vacuous pass
}

// The lookup has to accept whatever a caller passes: a bare name, a relative
// path, or a Windows-style one.
TEST(embedded_lookup_matches_on_basename) {
  CHECK(embedded_map_text("arena.txt") != nullptr);
  CHECK(embedded_map_text("maps/arena.txt") != nullptr);
  CHECK(embedded_map_text("./maps/arena.txt") != nullptr);
  CHECK(embedded_map_text("maps\\arena.txt") != nullptr);
  CHECK(embedded_map_text("/some/absolute/path/maps/arena.txt") != nullptr);
  CHECK(embedded_map_text("not_a_map.txt") == nullptr);
  CHECK(embedded_map_text(nullptr) == nullptr);
}

// And the baked copy has to actually parse into a playable map, not just exist.
TEST(embedded_maps_parse_into_playable_maps) {
  for (int i = 0; i < embedded_map_count(); ++i) {
    const char* text = embedded_map_text(embedded_map_name(i));
    CHECK(text != nullptr);
    auto map = std::make_unique<Map>();
    CHECK(map_parse(text, map.get()));
    CHECK(map->spawn_count > 0);
    CHECK(map->box_count + map->ramp_count + map->brush_count > 0);
    CHECK(map->grid.built);
  }
}

#pragma once

#include "core/math.h"

constexpr int MAX_MAP_BOXES = 256;
constexpr int MAX_SPAWNS = 16;

struct MapBox {
  Vec3 min, max;
  Vec3 color;
};

struct Map {
  char name[32];
  MapBox boxes[MAX_MAP_BOXES];
  int box_count;
  Vec3 spawns[MAX_SPAWNS];
  float spawn_yaws[MAX_SPAWNS];
  int spawn_count;
  Vec3 health_spawns[MAX_SPAWNS];
  int health_count;
  Vec3 light_dir;
  Vec3 fog_color;
  float fog_density;
  Vec3 sky_color;
};

bool map_parse(const char* text, Map* out);
bool map_load(const char* path, Map* out);

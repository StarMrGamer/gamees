#include "game/map.h"

#include "core/log.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>

static void map_defaults(Map* out) {
  std::memset(out, 0, sizeof(*out));
  std::snprintf(out->name, sizeof(out->name), "arena");
  out->light_dir = vec3_normalize({-0.4f, -0.9f, -0.2f});
  out->fog_color = {0.58f, 0.66f, 0.72f};
  out->fog_density = 0.025f;
  out->sky_color = {0.48f, 0.62f, 0.76f};
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
  while (std::getline(input, line)) {
    ++line_no;
    size_t comment = line.find('#');
    if (comment != std::string::npos) line.resize(comment);

    std::istringstream ss(line);
    std::string directive;
    if (!(ss >> directive)) continue;

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

  if (out->spawn_count == 0) {
    log_error("map has no spawn points");
    return false;
  }
  return true;
}

bool map_load(const char* path, Map* out) {
  FILE* f = std::fopen(path, "rb");
  if (!f) {
    log_error("failed to open map '%s': %s", path, std::strerror(errno));
    return false;
  }
  std::fseek(f, 0, SEEK_END);
  long len = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (len < 0 || len > 1024 * 1024) {
    std::fclose(f);
    log_error("map '%s' has invalid size", path);
    return false;
  }
  std::string text;
  text.resize(static_cast<size_t>(len));
  if (len > 0 && std::fread(text.data(), 1, static_cast<size_t>(len), f) != static_cast<size_t>(len)) {
    std::fclose(f);
    log_error("failed to read map '%s'", path);
    return false;
  }
  std::fclose(f);
  return map_parse(text.c_str(), out);
}

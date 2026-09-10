#include "client/config.h"

#include "core/log.h"
#include "platform/paths.h"

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static constexpr float CONFIG_MIN_SENSITIVITY = 0.1f;
static constexpr float CONFIG_MAX_SENSITIVITY = 20.0f;
static constexpr const char* CONFIG_FILE_NAME = "arena.cfg";

const char* client_config_path() {
  const char* env = std::getenv("ARENA_CONFIG");
  return env && env[0] ? env : CONFIG_FILE_NAME;
}

static bool config_file_exists(const char* path) {
  FILE* f = std::fopen(path, "r");
  if (!f) return false;
  std::fclose(f);
  return true;
}

// Resolve the config to read/write without depending on the working directory:
// an explicit ARENA_CONFIG wins, then arena.cfg in the cwd, then next to the
// executable (and up to three parents, so build/ finds the repo's file). Falls
// back to the cwd path so a fresh install still saves somewhere sensible.
static std::string resolve_config_path() {
  const char* env = std::getenv("ARENA_CONFIG");
  if (env && env[0]) return env;
  if (config_file_exists(CONFIG_FILE_NAME)) return CONFIG_FILE_NAME;

  char dir[1024];
  if (executable_directory(dir, sizeof(dir))) {
    std::string d = dir;
    for (int i = 0; i < 4; ++i) {
      std::string candidate = d + "/" + CONFIG_FILE_NAME;
      if (config_file_exists(candidate.c_str())) return candidate;
      size_t slash = d.find_last_of("/\\");
      if (slash == std::string::npos || slash == 0) break;
      d.resize(slash);
    }
  }
  return CONFIG_FILE_NAME;
}

const char* client_jump_bind_name(uint8_t bind) {
  switch (bind) {
    case JUMP_BIND_MWHEEL_UP: return "mwheelup";
    case JUMP_BIND_MWHEEL_DOWN: return "mwheeldown";
    default: return "space";
  }
}

static bool text_equals_ci(const char* a, const char* b) {
  if (!a || !b) return false;
  while (*a && *b) {
    unsigned char ca = static_cast<unsigned char>(*a++);
    unsigned char cb = static_cast<unsigned char>(*b++);
    if (std::tolower(ca) != std::tolower(cb)) return false;
  }
  return *a == 0 && *b == 0;
}

bool client_jump_bind_parse(const char* text, uint8_t* out) {
  if (!text || !out) return false;
  if (text_equals_ci(text, "space")) {
    *out = JUMP_BIND_SPACE;
    return true;
  }
  if (text_equals_ci(text, "mwheelup") || text_equals_ci(text, "wheelup") ||
      text_equals_ci(text, "up")) {
    *out = JUMP_BIND_MWHEEL_UP;
    return true;
  }
  if (text_equals_ci(text, "mwheeldown") || text_equals_ci(text, "wheeldown") ||
      text_equals_ci(text, "down")) {
    *out = JUMP_BIND_MWHEEL_DOWN;
    return true;
  }
  return false;
}

const char* client_airjump_bind_name(uint8_t bind) {
  switch (bind) {
    case AIRJUMP_BIND_SPACE: return "space";
    case AIRJUMP_BIND_MWHEEL_UP: return "mwheelup";
    case AIRJUMP_BIND_MWHEEL_DOWN: return "mwheeldown";
    default: return "lalt";
  }
}

bool client_airjump_bind_parse(const char* text, uint8_t* out) {
  if (!text || !out) return false;
  // "jump"/"same" used to mean "mirror the jump button"; that is no longer a
  // valid bind, so a legacy config falls back to the default.
  if (text_equals_ci(text, "jump") || text_equals_ci(text, "same") ||
      text_equals_ci(text, "lalt") || text_equals_ci(text, "alt")) {
    *out = AIRJUMP_BIND_LALT;
    return true;
  }
  if (text_equals_ci(text, "space")) {
    *out = AIRJUMP_BIND_SPACE;
    return true;
  }
  if (text_equals_ci(text, "mwheelup") || text_equals_ci(text, "wheelup") ||
      text_equals_ci(text, "up")) {
    *out = AIRJUMP_BIND_MWHEEL_UP;
    return true;
  }
  if (text_equals_ci(text, "mwheeldown") || text_equals_ci(text, "wheeldown") ||
      text_equals_ci(text, "down")) {
    *out = AIRJUMP_BIND_MWHEEL_DOWN;
    return true;
  }
  return false;
}

static bool parse_sensitivity_value(const char* text, float* out) {
  if (!text || !out) return false;
  char* end = nullptr;
  errno = 0;
  float value = std::strtof(text, &end);
  if (errno != 0 || end == text) return false;
  while (*end && std::isspace(static_cast<unsigned char>(*end))) ++end;
  if (*end != 0) return false;
  if (value < CONFIG_MIN_SENSITIVITY || value > CONFIG_MAX_SENSITIVITY) return false;
  *out = value;
  return true;
}

static void strip_comment(char* line) {
  for (char* p = line; p && *p; ++p) {
    if (*p == '#') {
      *p = 0;
      return;
    }
  }
}

static char* trim_left(char* text) {
  while (text && *text && std::isspace(static_cast<unsigned char>(*text))) ++text;
  return text;
}

static void trim_right(char* text) {
  if (!text) return;
  size_t len = std::strlen(text);
  while (len > 0 && std::isspace(static_cast<unsigned char>(text[len - 1]))) {
    text[--len] = 0;
  }
}

static bool split_config_line(char* line, char** key_out, char** value_out) {
  strip_comment(line);
  char* key = trim_left(line);
  trim_right(key);
  if (!key || !key[0]) return false;

  char* value = std::strchr(key, '=');
  if (value) {
    *value++ = 0;
  } else {
    value = key;
    while (*value && !std::isspace(static_cast<unsigned char>(*value))) ++value;
    if (*value) *value++ = 0;
  }
  trim_right(key);
  value = trim_left(value);
  trim_right(value);
  if (!key[0] || !value[0]) return false;
  *key_out = key;
  *value_out = value;
  return true;
}

bool client_config_load(ClientSettings& settings) {
  std::string path = resolve_config_path();
  FILE* f = std::fopen(path.c_str(), "r");
  if (!f) return false;

  char line[256];
  while (std::fgets(line, sizeof(line), f)) {
    char* key = nullptr;
    char* value = nullptr;
    if (!split_config_line(line, &key, &value)) continue;
    if (text_equals_ci(key, "sensitivity") || text_equals_ci(key, "sens")) {
      float parsed = 0.0f;
      if (parse_sensitivity_value(value, &parsed)) settings.sensitivity = parsed;
    } else if (text_equals_ci(key, "jump") || text_equals_ci(key, "jump_bind")) {
      uint8_t parsed = JUMP_BIND_SPACE;
      if (client_jump_bind_parse(value, &parsed)) settings.jump_bind = parsed;
    } else if (text_equals_ci(key, "doublejump") || text_equals_ci(key, "airjump") ||
               text_equals_ci(key, "airjump_bind")) {
      uint8_t parsed = AIRJUMP_BIND_LALT;
      if (client_airjump_bind_parse(value, &parsed)) settings.airjump_bind = parsed;
    }
  }
  std::fclose(f);
  return true;
}

bool client_config_save(const ClientSettings& settings) {
  std::string path = resolve_config_path();
  FILE* f = std::fopen(path.c_str(), "w");
  if (!f) {
    log_warn("failed to save config '%s'", path.c_str());
    return false;
  }
  std::fprintf(f, "# arena client config\n");
  std::fprintf(f, "sensitivity %.3f\n", settings.sensitivity);
  std::fprintf(f, "jump %s\n", client_jump_bind_name(settings.jump_bind));
  std::fprintf(f, "doublejump %s\n", client_airjump_bind_name(settings.airjump_bind));
  std::fclose(f);
  return true;
}

#include "client/config.h"

#include "core/log.h"

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static constexpr float CONFIG_MIN_SENSITIVITY = 0.1f;
static constexpr float CONFIG_MAX_SENSITIVITY = 20.0f;

const char* client_config_path() {
  const char* env = std::getenv("ARENA_CONFIG");
  return env && env[0] ? env : "arena.cfg";
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
  const char* path = client_config_path();
  FILE* f = std::fopen(path, "r");
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
    }
  }
  std::fclose(f);
  return true;
}

bool client_config_save(const ClientSettings& settings) {
  const char* path = client_config_path();
  FILE* f = std::fopen(path, "w");
  if (!f) {
    log_warn("failed to save config '%s'", path);
    return false;
  }
  std::fprintf(f, "# arena client config\n");
  std::fprintf(f, "sensitivity %.3f\n", settings.sensitivity);
  std::fprintf(f, "jump %s\n", client_jump_bind_name(settings.jump_bind));
  std::fclose(f);
  return true;
}

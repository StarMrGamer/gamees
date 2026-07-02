#include "test_harness.h"

#include "client/config.h"

#include <cstdio>
#include <cstdlib>

static void set_config_env(const char* path) {
#ifdef _WIN32
  _putenv_s("ARENA_CONFIG", path ? path : "");
#else
  if (path) {
    setenv("ARENA_CONFIG", path, 1);
  } else {
    unsetenv("ARENA_CONFIG");
  }
#endif
}

TEST(client_config_roundtrips_sensitivity_and_jump_bind) {
  const char* path = "arena_test_config.cfg";
  std::remove(path);
  set_config_env(path);

  ClientSettings saved{};
  saved.sensitivity = 7.25f;
  saved.jump_bind = JUMP_BIND_MWHEEL_DOWN;
  CHECK(client_config_save(saved));

  ClientSettings loaded{};
  loaded.sensitivity = 1.0f;
  loaded.jump_bind = JUMP_BIND_SPACE;
  CHECK(client_config_load(loaded));
  CHECK_NEAR(loaded.sensitivity, 7.25f, 0.001f);
  CHECK_EQ_INT(loaded.jump_bind, JUMP_BIND_MWHEEL_DOWN);

  std::remove(path);
  set_config_env(nullptr);
}

TEST(client_config_parses_jump_aliases) {
  uint8_t bind = JUMP_BIND_SPACE;
  CHECK(client_jump_bind_parse("mwheelup", &bind));
  CHECK_EQ_INT(bind, JUMP_BIND_MWHEEL_UP);
  CHECK(client_jump_bind_parse("down", &bind));
  CHECK_EQ_INT(bind, JUMP_BIND_MWHEEL_DOWN);
  CHECK(client_jump_bind_parse("SPACE", &bind));
  CHECK_EQ_INT(bind, JUMP_BIND_SPACE);
}

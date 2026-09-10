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

TEST(double_jump_bind_never_mirrors_jump) {
  // Regression: the double jump used to default to "same as jump", so pressing
  // the normal jump button twice fired a double jump. It must only ever fire
  // from its own bind.
  ClientSettings s{};
  CHECK_EQ_INT(s.jump_bind, JUMP_BIND_SPACE);
  CHECK_EQ_INT(s.airjump_bind, AIRJUMP_BIND_LALT);
  CHECK(airjump_bind_pressed(s.airjump_bind, true, false, 0.0f));   // Left Alt
  CHECK(!airjump_bind_pressed(s.airjump_bind, false, true, 0.0f));  // Space (jump) must not
  // An explicit Space bind reacts to Space only, never the Alt default.
  CHECK(airjump_bind_pressed(AIRJUMP_BIND_SPACE, false, true, 0.0f));
  CHECK(!airjump_bind_pressed(AIRJUMP_BIND_SPACE, true, false, 0.0f));
  // Wheel binds read the wheel and ignore keys.
  CHECK(airjump_bind_pressed(AIRJUMP_BIND_MWHEEL_UP, true, true, 1.0f));
  CHECK(!airjump_bind_pressed(AIRJUMP_BIND_MWHEEL_UP, true, true, -1.0f));
}

TEST(client_config_parses_airjump_binds) {
  uint8_t bind = 255;
  CHECK(client_airjump_bind_parse("lalt", &bind));
  CHECK_EQ_INT(bind, AIRJUMP_BIND_LALT);
  CHECK(client_airjump_bind_parse("alt", &bind));
  CHECK_EQ_INT(bind, AIRJUMP_BIND_LALT);
  CHECK(client_airjump_bind_parse("space", &bind));
  CHECK_EQ_INT(bind, AIRJUMP_BIND_SPACE);
  CHECK(client_airjump_bind_parse("mwheeldown", &bind));
  CHECK_EQ_INT(bind, AIRJUMP_BIND_MWHEEL_DOWN);
  CHECK(client_airjump_bind_parse("jump", &bind));  // legacy alias -> default
  CHECK_EQ_INT(bind, AIRJUMP_BIND_LALT);
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

// The frame cap used to be an accidental SDL_Delay(1) in the render loop.
// Now it is a setting, so the clamp around it needs to hold: zero and negative
// mean unlimited, a silly-low value is raised rather than making the game
// unplayable, and anything past the top of the range means unlimited too.
TEST(max_fps_clamp) {
  CHECK_EQ_INT(sanitize_max_fps(0), MAX_FPS_UNLIMITED);
  CHECK_EQ_INT(sanitize_max_fps(-5), MAX_FPS_UNLIMITED);
  CHECK_EQ_INT(sanitize_max_fps(1), MIN_FPS_CAP);
  CHECK_EQ_INT(sanitize_max_fps(29), MIN_FPS_CAP);
  CHECK_EQ_INT(sanitize_max_fps(30), 30);
  CHECK_EQ_INT(sanitize_max_fps(144), 144);
  CHECK_EQ_INT(sanitize_max_fps(1000), 1000);
  CHECK_EQ_INT(sanitize_max_fps(5000), MAX_FPS_UNLIMITED);
}

TEST(config_round_trips_max_fps_and_vsync) {
  const char* path = "/tmp/arena_test_fps.cfg";
  setenv("ARENA_CONFIG", path, 1);
  std::remove(path);

  ClientSettings out{};
  out.max_fps = 240;
  out.vsync = true;
  CHECK(client_config_save(out));

  ClientSettings in{};
  CHECK(client_config_load(in));
  CHECK_EQ_INT(in.max_fps, 240);
  CHECK(in.vsync);

  // "unlimited" must survive the round trip as well.
  out.max_fps = MAX_FPS_UNLIMITED;
  out.vsync = false;
  CHECK(client_config_save(out));
  ClientSettings in2{};
  in2.max_fps = 123;
  in2.vsync = true;
  CHECK(client_config_load(in2));
  CHECK_EQ_INT(in2.max_fps, MAX_FPS_UNLIMITED);
  CHECK(!in2.vsync);

  std::remove(path);
  unsetenv("ARENA_CONFIG");
}

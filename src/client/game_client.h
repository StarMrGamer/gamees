#pragma once

#include "game/game_state.h"
#include "platform/socket.h"
#include "server/dedicated.h"

enum JumpBind : uint8_t {
  JUMP_BIND_SPACE = 0,
  JUMP_BIND_MWHEEL_UP = 1,
  JUMP_BIND_MWHEEL_DOWN = 2,
};

// Double (mid-air) jump bind. It is always an input of its own: the normal
// jump button must never trigger the double jump. Defaults to Left Alt so it
// is distinct from the default Space jump.
enum AirJumpBind : uint8_t {
  AIRJUMP_BIND_LALT = 0,
  AIRJUMP_BIND_SPACE = 1,
  AIRJUMP_BIND_MWHEEL_UP = 2,
  AIRJUMP_BIND_MWHEEL_DOWN = 3,
};

// 0 means no limit. The frame loop used to end in an unconditional
// SDL_Delay(1), which pinned the client near 1000 fps on any machine fast
// enough to notice; the cap is now an explicit setting that defaults to off.
constexpr int MAX_FPS_UNLIMITED = 0;
constexpr int MIN_FPS_CAP = 30;
constexpr int MAX_FPS_CAP = 1000;

struct ClientSettings {
  float sensitivity = 3.0f;
  uint8_t player_class = CLASS_RANGER;
  uint8_t jump_bind = JUMP_BIND_SPACE;
  uint8_t airjump_bind = AIRJUMP_BIND_LALT;
  int max_fps = MAX_FPS_UNLIMITED;
  bool vsync = false;
};

// Clamps a requested cap to something sane. Anything at or below zero means
// unlimited; a cap below MIN_FPS_CAP is almost certainly a typo and would make
// the game unplayable, so it is raised rather than honoured.
inline int sanitize_max_fps(int requested) {
  if (requested <= 0) return MAX_FPS_UNLIMITED;
  if (requested < MIN_FPS_CAP) return MIN_FPS_CAP;
  if (requested > MAX_FPS_CAP) return MAX_FPS_UNLIMITED;
  return requested;
}

// Pure resolution of a double-jump bind against raw input state. Kept free of
// SDL so it can be unit-tested and so the jump button can never leak into it.
inline bool airjump_bind_pressed(uint8_t bind, bool lalt_down, bool space_down, float wheel_y) {
  switch (bind) {
    case AIRJUMP_BIND_LALT: return lalt_down;
    case AIRJUMP_BIND_SPACE: return space_down;
    case AIRJUMP_BIND_MWHEEL_UP: return wheel_y > 0.0f;
    case AIRJUMP_BIND_MWHEEL_DOWN: return wheel_y < 0.0f;
    default: return false;
  }
}

int game_client_main(NetAddress server, const char* player_name, ServerThread* owned_server,
                     const char* map_path = "maps/arena.txt",
                     ClientSettings settings = ClientSettings{});

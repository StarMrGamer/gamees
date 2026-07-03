#pragma once

#include "game/game_state.h"
#include "platform/socket.h"
#include "server/dedicated.h"

enum JumpBind : uint8_t {
  JUMP_BIND_SPACE = 0,
  JUMP_BIND_MWHEEL_UP = 1,
  JUMP_BIND_MWHEEL_DOWN = 2,
};

// Double (mid-air) jump bind. Defaults to "same as jump" so the out-of-box
// feel is unchanged; can be moved onto its own key/wheel independently.
enum AirJumpBind : uint8_t {
  AIRJUMP_BIND_JUMP = 0,
  AIRJUMP_BIND_SPACE = 1,
  AIRJUMP_BIND_MWHEEL_UP = 2,
  AIRJUMP_BIND_MWHEEL_DOWN = 3,
};

struct ClientSettings {
  float sensitivity = 3.0f;
  uint8_t player_class = CLASS_RANGER;
  uint8_t jump_bind = JUMP_BIND_SPACE;
  uint8_t airjump_bind = AIRJUMP_BIND_JUMP;
};

int game_client_main(NetAddress server, const char* player_name, ServerThread* owned_server,
                     const char* map_path = "maps/arena.txt",
                     ClientSettings settings = ClientSettings{});

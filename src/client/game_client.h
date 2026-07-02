#pragma once

#include "game/game_state.h"
#include "platform/socket.h"
#include "server/dedicated.h"

enum JumpBind : uint8_t {
  JUMP_BIND_SPACE = 0,
  JUMP_BIND_MWHEEL_UP = 1,
  JUMP_BIND_MWHEEL_DOWN = 2,
};

struct ClientSettings {
  float sensitivity = 3.0f;
  uint8_t player_class = CLASS_RANGER;
  uint8_t jump_bind = JUMP_BIND_SPACE;
};

int game_client_main(NetAddress server, const char* player_name, ServerThread* owned_server,
                     const char* map_path = "maps/arena.txt",
                     ClientSettings settings = ClientSettings{});

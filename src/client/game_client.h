#pragma once

#include "platform/socket.h"
#include "server/dedicated.h"

int game_client_main(NetAddress server, const char* player_name, ServerThread* owned_server,
                     const char* map_path = "maps/arena.txt");

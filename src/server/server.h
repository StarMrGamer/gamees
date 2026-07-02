#pragma once

#include "core/rng.h"
#include "game/game_state.h"
#include "game/map.h"
#include "platform/socket.h"

struct ClientSlot {
  bool used;
  NetAddress addr;
  int player_index;
  double last_recv_time;
  PlayerInput latest_input;
  uint32_t highest_input_seq;
};

struct Server {
  UdpSocket sock;
  GameState state;
  Map map;
  ClientSlot clients[MAX_PLAYERS];
  Rng rng;
  double now;
};

bool server_init(Server& sv, uint16_t port, const char* map_path, int frag_limit);
void server_pump(Server& sv, double now);
void server_tick(Server& sv);
void server_broadcast(Server& sv);
void server_shutdown(Server& sv);

#pragma once

#include "game/game_state.h"
#include "platform/socket.h"

enum ClientState {
  CLIENT_DISCONNECTED,
  CLIENT_CONNECTING,
  CLIENT_CONNECTED,
  CLIENT_REJECTED,
};

struct Client {
  UdpSocket sock;
  NetAddress server_addr;
  ClientState state;
  int player_index;
  char map_name[32];
  char player_name[16];
  char reject_reason[64];
  GameState snap_a, snap_b;
  double snap_b_recv_time;
  bool have_two_snaps;
  PlayerInput input_history[3];
  uint32_t next_input_seq;
  uint32_t last_seen_event_id;
  double last_recv_time, connect_start_time, last_hello_time;
};

bool client_start(Client& c, NetAddress server, const char* player_name);
void client_send_input(Client& c, const PlayerInput& in);

struct ClientEvents {
  GameEvent events[MAX_EVENTS];
  int count;
};

void client_receive(Client& c, double now, ClientEvents* new_events);
void client_view_state(const Client& c, double now, GameState* out);
void client_disconnect(Client& c);

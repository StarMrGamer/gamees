#pragma once

#include "game/game_state.h"
#include "game/map.h"
#include "platform/socket.h"

enum ClientState {
  CLIENT_DISCONNECTED,
  CLIENT_CONNECTING,
  CLIENT_CONNECTED,
  CLIENT_REJECTED,
};

// Ring of sent inputs kept for prediction replay; must comfortably exceed the
// worst pending window (RTT in ticks) we are willing to predict across.
constexpr int CLIENT_INPUT_RING = 128;

// Short ring of received snapshots, kept so remote entities can be interpolated
// against a jitter-buffered render clock even when packets arrive late, early,
// or out of order, and so a few dropped snapshots cost nothing.
constexpr int CLIENT_SNAP_RING = 8;
// How far behind the newest snapshot the render clock aims to sit, in ticks.
// A couple of ticks of buffer hides normal network jitter and loss.
constexpr float INTERP_TARGET_DELAY_TICKS = 2.0f;
// Never render closer than this to the newest tick; caps extrapolation when
// snapshots stop arriving.
constexpr float INTERP_MIN_RENDER_DELAY = 0.5f;
// How hard the render clock is pulled back toward the target delay, per tick
// of error, and the most it may be sped up or slowed down (5%). Small enough
// that the correction is imperceptible, large enough to refill the buffer
// within a second of a network hiccup.
constexpr float INTERP_CLOCK_GAIN = 0.05f;
constexpr float INTERP_CLOCK_MAX_ADJUST = 0.05f;

struct SnapshotSlot {
  uint32_t tick;  // 0 = empty
  GameState state;
};

struct Client {
  UdpSocket sock;
  NetAddress server_addr;
  ClientState state;
  int player_index;
  char map_name[32];
  char player_name[16];
  char reject_reason[64];
  GameState snap_b;                      // newest snapshot (prediction + events)
  SnapshotSlot snaps[CLIENT_SNAP_RING];  // interpolation history
  uint32_t newest_snap_tick;
  float render_tick;                     // jitter-buffered view tick, in ticks
  bool render_clock_init;
  double last_view_time;
  PlayerInput input_history[3];
  uint32_t next_input_seq;
  uint32_t last_seen_event_id;
  double last_recv_time, connect_start_time, last_hello_time;
  double net_now;
  double next_input_send_time;
  uint16_t pending_buttons;
  uint8_t pending_weapon_switch;
  uint8_t pending_class_switch;
  PlayerInput sent_inputs[CLIENT_INPUT_RING];
  Map prediction_map;
  bool prediction_ready;
};

bool client_start(Client& c, NetAddress server, const char* player_name);
void client_send_input(Client& c, const PlayerInput& in);

struct ClientEvents {
  GameEvent events[MAX_EVENTS];
  int count;
};

void client_receive(Client& c, double now, ClientEvents* new_events);
// Server tick the render clock is currently showing; sent with each input so
// the server can lag-compensate shots to this viewpoint.
uint32_t client_view_tick(const Client& c);
void client_view_state(const Client& c, GameState* out);
void client_disconnect(Client& c);

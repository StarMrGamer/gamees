#include "client/client.h"

#include "core/log.h"
#include "game/movement.h"
#include "game/snapshot.h"
#include "game/tuning.h"
#include "net/protocol.h"

#include <cstdio>
#include <cstring>

static void send_hello(Client& c, const char* name) {
  uint8_t buf[MAX_PACKET];
  NetWriter w;
  nw_init(w, buf, sizeof(buf));
  packet_header_write(w, PKT_CL_HELLO);
  nw_string(w, name, 15);
  if (!w.overflow) udp_send(c.sock, c.server_addr, buf, w.len);
}

bool client_start(Client& c, NetAddress server, const char* player_name) {
  std::memset(&c, 0, sizeof(c));
  if (!net_init()) return false;
  c.sock = udp_open(0);
  if (!c.sock.valid) {
    net_shutdown();
    return false;
  }
  c.server_addr = server;
  c.state = CLIENT_CONNECTING;
  c.player_index = -1;
  c.next_input_seq = 1;
  std::snprintf(c.player_name, sizeof(c.player_name), "%s",
                (player_name && player_name[0]) ? player_name : "player");
  send_hello(c, c.player_name);
  return true;
}

static void send_input_packet(Client& c) {
  uint8_t buf[MAX_PACKET];
  NetWriter w;
  nw_init(w, buf, sizeof(buf));
  packet_header_write(w, PKT_CL_INPUT);
  int count = 0;
  for (int i = 0; i < 3; ++i) if (c.input_history[i].sequence != 0) ++count;
  nw_u8(w, static_cast<uint8_t>(count));
  for (int i = 0; i < count; ++i) {
    const PlayerInput& h = c.input_history[i];
    nw_u32(w, h.sequence);
    nw_u8(w, h.buttons);
    nw_u8(w, h.weapon_switch);
    nw_u8(w, h.class_switch);
    nw_f32(w, h.yaw);
    nw_f32(w, h.pitch);
  }
  if (!w.overflow) udp_send(c.sock, c.server_addr, buf, w.len);
}

// Called once per render frame at any frame rate; sends inputs at TICK_RATE so
// each sent input corresponds to one server tick (and one prediction step).
// Button taps between sends accumulate in pending_buttons so they can't be lost.
void client_send_input(Client& c, const PlayerInput& in) {
  if (c.state != CLIENT_CONNECTED && c.state != CLIENT_CONNECTING) return;
  c.pending_buttons |= in.buttons;
  if (in.weapon_switch != 0) c.pending_weapon_switch = in.weapon_switch;
  if (in.class_switch != 0) c.pending_class_switch = in.class_switch;

  constexpr double SEND_INTERVAL = 1.0 / TICK_RATE;
  double now = c.net_now;
  if (c.next_input_send_time <= 0.0) c.next_input_send_time = now;
  // A slow frame may cover several ticks: send one input per covered tick
  // (capped) so the server doesn't starve and prediction stays 1:1 with ticks.
  int sends = 0;
  while (now >= c.next_input_send_time && sends < 4) {
    PlayerInput cmd{};
    cmd.sequence = c.next_input_seq++;
    cmd.buttons = c.pending_buttons;
    cmd.weapon_switch = c.pending_weapon_switch;
    cmd.class_switch = c.pending_class_switch;
    cmd.yaw = in.yaw;
    cmd.pitch = in.pitch;
    c.input_history[2] = c.input_history[1];
    c.input_history[1] = c.input_history[0];
    c.input_history[0] = cmd;
    c.sent_inputs[cmd.sequence % CLIENT_INPUT_RING] = cmd;
    send_input_packet(c);
    c.pending_buttons = in.buttons;
    c.pending_weapon_switch = 0;
    c.pending_class_switch = 0;
    c.next_input_send_time += SEND_INTERVAL;
    ++sends;
  }
  if (now - c.next_input_send_time > 4.0 * SEND_INTERVAL) c.next_input_send_time = now;
}

static void add_new_events(Client& c, const GameState& s, ClientEvents* out) {
  if (!out) return;
  for (int i = 0; i < MAX_EVENTS; ++i) {
    const GameEvent& e = s.events[i];
    if (e.id == 0 || e.id <= c.last_seen_event_id) continue;
    if (out->count < MAX_EVENTS) out->events[out->count++] = e;
    if (e.id > c.last_seen_event_id) c.last_seen_event_id = e.id;
  }
}

// Prediction needs local collision geometry. The map file is looked up by the
// server-announced map name under maps/; if it's missing or names a different
// map, prediction is disabled and the client falls back to rendering raw
// server state for the local player.
static void load_prediction_map(Client& c) {
  char path[64];
  std::snprintf(path, sizeof(path), "maps/%s.txt", c.map_name);
  c.prediction_ready = map_load(path, &c.prediction_map) &&
                       std::strcmp(c.prediction_map.name, c.map_name) == 0;
  if (c.prediction_ready) {
    log_info("movement prediction enabled (map '%s')", c.map_name);
  } else {
    log_warn("movement prediction disabled: no matching local map for '%s'", c.map_name);
  }
}

void client_receive(Client& c, double now, ClientEvents* new_events) {
  if (new_events) new_events->count = 0;
  if (!c.sock.valid) return;
  c.net_now = now;

  if (c.state == CLIENT_CONNECTING) {
    if (c.connect_start_time <= 0.0) c.connect_start_time = now;
    if (now - c.last_hello_time >= 0.75) {
      send_hello(c, c.player_name);
      c.last_hello_time = now;
    }
  }

  uint8_t buf[MAX_PACKET];
  NetAddress from{};
  for (;;) {
    int got = udp_recv(c.sock, buf, sizeof(buf), &from);
    if (got < 0) break;
    if (!net_address_equal(from, c.server_addr)) continue;
    NetReader r;
    nr_init(r, buf, got);
    PacketType type{};
    if (!packet_header_read(r, &type)) continue;
    c.last_recv_time = now;
    if (type == PKT_SV_ACCEPT) {
      c.player_index = nr_u8(r);
      (void)nr_u8(r);
      nr_string(r, c.map_name, sizeof(c.map_name));
      if (!r.error && c.state != CLIENT_CONNECTED) {
        c.state = CLIENT_CONNECTED;
        load_prediction_map(c);
      }
    } else if (type == PKT_SV_REJECT) {
      nr_string(r, c.reject_reason, sizeof(c.reject_reason));
      c.state = CLIENT_REJECTED;
    } else if (type == PKT_SV_SNAPSHOT) {
      GameState next{};
      if (snapshot_read(next, r) && next.tick > c.snap_b.tick) {
        c.snap_a = c.snap_b;
        c.snap_b = next;
        c.have_two_snaps = c.snap_a.tick != 0;
        c.snap_b_recv_time = now;
        add_new_events(c, next, new_events);
      }
    } else if (type == PKT_SV_SHUTDOWN) {
      c.state = CLIENT_DISCONNECTED;
    }
  }

  if (c.state == CLIENT_CONNECTING && c.connect_start_time > 0.0 &&
      now - c.connect_start_time > CLIENT_TIMEOUT) {
    c.state = CLIENT_DISCONNECTED;
  } else if (c.state == CLIENT_CONNECTED &&
             c.last_recv_time > 0.0 && now - c.last_recv_time > CLIENT_TIMEOUT) {
    c.state = CLIENT_DISCONNECTED;
  }
}

// Re-run the shared movement sim over inputs the server hasn't acknowledged
// yet, starting from the newest snapshot. Because snapshots replicate every
// field player_move() reads and the server consumes one input per tick, the
// replayed result matches the server's future state exactly under no loss.
static bool predict_local_player(const Client& c, Player* out) {
  const Player& base = c.snap_b.players[c.player_index];
  if (!c.prediction_ready || !base.active || !base.alive || c.snap_b.match_over) return false;
  uint32_t acked = base.last_input_seq;
  uint32_t newest = c.next_input_seq - 1;
  if (newest <= acked) return false;
  if (newest - acked > CLIENT_INPUT_RING - 8) return false;
  Player p = base;
  for (uint32_t seq = acked + 1; seq <= newest; ++seq) {
    const PlayerInput& cmd = c.sent_inputs[seq % CLIENT_INPUT_RING];
    if (cmd.sequence != seq) return false;
    player_move(p, cmd, c.prediction_map, TICK_DT);
  }
  *out = p;
  return true;
}

void client_view_state(const Client& c, double now, GameState* out) {
  if (!out) return;
  if (!c.have_two_snaps) {
    *out = c.snap_b;
  } else {
    // Render remote entities one snapshot interval behind: blend from snap_a
    // to snap_b over the interval following snap_b's arrival.
    float t = static_cast<float>((now - c.snap_b_recv_time) * TICK_RATE);
    snapshot_interpolate(c.snap_a, c.snap_b, t, *out);
  }
  if (c.player_index < 0 || c.player_index >= MAX_PLAYERS) return;
  out->players[c.player_index] = c.snap_b.players[c.player_index];
  Player predicted{};
  if (predict_local_player(c, &predicted)) {
    out->players[c.player_index] = predicted;
  }
}

void client_disconnect(Client& c) {
  if (c.sock.valid) {
    uint8_t buf[MAX_PACKET];
    NetWriter w;
    nw_init(w, buf, sizeof(buf));
    packet_header_write(w, PKT_CL_DISCONNECT);
    if (!w.overflow) udp_send(c.sock, c.server_addr, buf, w.len);
    udp_close(c.sock);
    net_shutdown();
  }
  c.state = CLIENT_DISCONNECTED;
}

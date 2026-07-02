#include "client/client.h"

#include "core/log.h"
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

void client_send_input(Client& c, const PlayerInput& in) {
  if (c.state != CLIENT_CONNECTED && c.state != CLIENT_CONNECTING) return;
  PlayerInput cmd = in;
  cmd.sequence = c.next_input_seq++;
  c.input_history[2] = c.input_history[1];
  c.input_history[1] = c.input_history[0];
  c.input_history[0] = cmd;

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

static void add_new_events(Client& c, const GameState& s, ClientEvents* out) {
  if (!out) return;
  for (int i = 0; i < MAX_EVENTS; ++i) {
    const GameEvent& e = s.events[i];
    if (e.id == 0 || e.id <= c.last_seen_event_id) continue;
    if (out->count < MAX_EVENTS) out->events[out->count++] = e;
    if (e.id > c.last_seen_event_id) c.last_seen_event_id = e.id;
  }
}

void client_receive(Client& c, double now, ClientEvents* new_events) {
  if (new_events) new_events->count = 0;
  if (!c.sock.valid) return;

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
      if (!r.error) c.state = CLIENT_CONNECTED;
    } else if (type == PKT_SV_REJECT) {
      nr_string(r, c.reject_reason, sizeof(c.reject_reason));
      c.state = CLIENT_REJECTED;
    } else if (type == PKT_SV_SNAPSHOT) {
      GameState next{};
      if (snapshot_read(next, r)) {
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

void client_view_state(const Client& c, double now, GameState* out) {
  if (!out) return;
  if (!c.have_two_snaps) {
    *out = c.snap_b;
    return;
  }
  float t = static_cast<float>((now - c.snap_b_recv_time) * TICK_RATE + 1.0);
  snapshot_interpolate(c.snap_a, c.snap_b, t, *out);
  if (c.player_index >= 0 && c.player_index < MAX_PLAYERS) {
    out->players[c.player_index] = c.snap_b.players[c.player_index];
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

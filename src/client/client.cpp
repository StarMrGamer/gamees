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
    nw_u16(w, h.buttons);
    nw_u8(w, h.weapon_switch);
    nw_u8(w, h.class_switch);
    nw_f32(w, h.yaw);
    nw_f32(w, h.pitch);
    nw_u32(w, h.view_tick);
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
  uint32_t view_tick = client_view_tick(c);
  while (now >= c.next_input_send_time && sends < 4) {
    PlayerInput cmd{};
    cmd.sequence = c.next_input_seq++;
    cmd.buttons = c.pending_buttons;
    cmd.weapon_switch = c.pending_weapon_switch;
    cmd.class_switch = c.pending_class_switch;
    cmd.yaw = in.yaw;
    cmd.pitch = in.pitch;
    cmd.view_tick = view_tick;
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
      // snapshot_read() zeroes the whole structure itself; no need to
      // value-initialise ~6 KB here first.
      GameState next;
      if (snapshot_read(next, r) && next.tick > c.newest_snap_tick) {
        SnapshotSlot& slot = c.snaps[next.tick % CLIENT_SNAP_RING];
        slot.tick = next.tick;
        slot.state = next;
        c.snap_b = next;
        c.newest_snap_tick = next.tick;
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

  // Advance the jitter-buffered render clock. It tracks real time and is only
  // clamped when it would outrun the newest snapshot (packet loss) or fall
  // further behind than the snapshot ring can bracket. This keeps remote
  // entities smooth even when snapshots arrive irregularly.
  if (c.newest_snap_tick != 0) {
    if (!c.render_clock_init) {
      c.render_tick = static_cast<float>(c.newest_snap_tick) - INTERP_TARGET_DELAY_TICKS;
      c.render_clock_init = true;
      c.last_view_time = now;
    } else {
      double dt = now - c.last_view_time;
      if (dt < 0.0) dt = 0.0;
      if (dt > 0.25) dt = 0.25;
      c.last_view_time = now;

      // The clock runs at real time, but is nudged a few percent fast or slow
      // to converge on sitting INTERP_TARGET_DELAY_TICKS behind the newest
      // snapshot. Without this the buffer is a one-shot: the first stall
      // pushes the clock up against the max clamp and it never drifts back,
      // so every later late packet stutters. The correction is small enough
      // not to be visible as a speed change.
      // The error is measured against where the clock is *about* to be, not
      // where it was. Measuring pre-advance biases the steady state a full
      // tick early, because the newest tick has already moved on by then.
      float newest = static_cast<float>(c.newest_snap_tick);
      float advance = static_cast<float>(dt * TICK_RATE);
      float error = (newest - INTERP_TARGET_DELAY_TICKS) - (c.render_tick + advance);
      float rate = 1.0f + clampf(error * INTERP_CLOCK_GAIN, -INTERP_CLOCK_MAX_ADJUST,
                                 INTERP_CLOCK_MAX_ADJUST);
      c.render_tick += advance * rate;

      // Hard limits, only reached when the drift correction cannot keep up:
      // never render past what has arrived, and never fall so far behind that
      // the snapshot ring no longer brackets the render tick.
      float max_tick = newest - INTERP_MIN_RENDER_DELAY;
      float min_tick = newest - static_cast<float>(CLIENT_SNAP_RING - 2);
      if (c.render_tick > max_tick) c.render_tick = max_tick;
      if (c.render_tick < min_tick) c.render_tick = min_tick;
    }
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

uint32_t client_view_tick(const Client& c) {
  if (!c.render_clock_init || c.render_tick <= 0.0f) return c.newest_snap_tick;
  return static_cast<uint32_t>(c.render_tick + 0.5f);
}

void client_view_state(const Client& c, GameState* out) {
  if (!out) return;
  if (c.newest_snap_tick == 0 || !c.render_clock_init) {
    *out = c.snap_b;
  } else {
    // Interpolate remote entities between the two snapshots that bracket the
    // render clock. The clock is decoupled from packet arrival, so this stays
    // smooth under jitter and tolerates a few dropped or reordered snapshots.
    const GameState* a = nullptr;
    const GameState* b = nullptr;
    float at = 0.0f;
    float bt = 0.0f;
    for (int i = 0; i < CLIENT_SNAP_RING; ++i) {
      const SnapshotSlot& slot = c.snaps[i];
      if (slot.tick == 0) continue;
      float t = static_cast<float>(slot.tick);
      if (t <= c.render_tick && (!a || t > at)) {
        a = &slot.state;
        at = t;
      }
      if (t >= c.render_tick && (!b || t < bt)) {
        b = &slot.state;
        bt = t;
      }
    }
    if (a && b && a != b) {
      float span = bt - at;
      float frac = span > 0.0001f ? (c.render_tick - at) / span : 0.0f;
      snapshot_interpolate(*a, *b, frac, *out);
    } else if (b) {
      *out = *b;
    } else if (a) {
      *out = *a;
    } else {
      *out = c.snap_b;
    }
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

#include "server/server.h"

#include "core/log.h"
#include "game/sim.h"
#include "game/snapshot.h"
#include "game/tuning.h"
#include "net/protocol.h"

#include <cstring>

static int find_slot(Server& sv, NetAddress addr) {
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    if (sv.clients[i].used && net_address_equal(sv.clients[i].addr, addr)) return i;
  }
  return -1;
}

static void send_reject(Server& sv, NetAddress to, const char* reason) {
  uint8_t buf[MAX_PACKET];
  NetWriter w;
  nw_init(w, buf, sizeof(buf));
  packet_header_write(w, PKT_SV_REJECT);
  nw_string(w, reason, 63);
  if (!w.overflow) udp_send(sv.sock, to, buf, w.len);
}

static void send_accept(Server& sv, ClientSlot& slot) {
  uint8_t buf[MAX_PACKET];
  NetWriter w;
  nw_init(w, buf, sizeof(buf));
  packet_header_write(w, PKT_SV_ACCEPT);
  nw_u8(w, static_cast<uint8_t>(slot.player_index));
  nw_u8(w, TICK_RATE);
  nw_string(w, sv.map.name, 31);
  if (!w.overflow) udp_send(sv.sock, slot.addr, buf, w.len);
}

bool server_init(Server& sv, uint16_t port, const char* map_path, int frag_limit) {
  std::memset(&sv, 0, sizeof(sv));
  if (!net_init()) return false;
  if (!map_load(map_path, &sv.map)) return false;
  sv.sock = udp_open(port);
  if (!sv.sock.valid) return false;
  sv.rng.state = 0x5eed12345678ull;
  game_init(sv.state, sv.map, frag_limit);
  return true;
}

static void handle_hello(Server& sv, NetAddress from, NetReader& r) {
  char name[16]{};
  nr_string(r, name, sizeof(name));
  if (r.error) return;

  int existing = find_slot(sv, from);
  if (existing >= 0) {
    send_accept(sv, sv.clients[existing]);
    return;
  }

  int free_slot = -1;
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    if (!sv.clients[i].used) {
      free_slot = i;
      break;
    }
  }
  if (free_slot < 0) {
    send_reject(sv, from, "server full");
    return;
  }

  int player = game_player_join(sv.state, sv.map, name);
  if (player < 0) {
    send_reject(sv, from, "server full");
    return;
  }

  ClientSlot& slot = sv.clients[free_slot];
  std::memset(&slot, 0, sizeof(slot));
  slot.used = true;
  slot.addr = from;
  slot.player_index = player;
  slot.last_recv_time = sv.now;
  send_accept(sv, slot);
}

// Insert one input in sequence order, dropping already-applied or duplicate
// sequences. The queue absorbs bursty arrival so server_tick can consume
// exactly one input per tick — which is what client prediction replays.
static void queue_input(ClientSlot& slot, const PlayerInput& in) {
  if (in.sequence <= slot.highest_input_seq) return;
  for (int i = 0; i < slot.input_queue_len; ++i) {
    if (slot.input_queue[i].sequence == in.sequence) return;
  }
  if (slot.input_queue_len == SERVER_INPUT_QUEUE) {
    for (int i = 1; i < SERVER_INPUT_QUEUE; ++i) slot.input_queue[i - 1] = slot.input_queue[i];
    --slot.input_queue_len;
  }
  int pos = slot.input_queue_len++;
  while (pos > 0 && slot.input_queue[pos - 1].sequence > in.sequence) {
    slot.input_queue[pos] = slot.input_queue[pos - 1];
    --pos;
  }
  slot.input_queue[pos] = in;
}

static void handle_input(Server& sv, int slot_index, NetReader& r) {
  if (slot_index < 0) return;
  ClientSlot& slot = sv.clients[slot_index];
  int count = nr_u8(r);
  if (count < 1 || count > 3) {
    r.error = true;
    return;
  }
  for (int i = 0; i < count; ++i) {
    PlayerInput in{};
    in.sequence = nr_u32(r);
    in.buttons = nr_u16(r);
    in.weapon_switch = nr_u8(r);
    in.class_switch = nr_u8(r);
    in.yaw = nr_f32(r);
    in.pitch = nr_f32(r);
    in.view_tick = nr_u32(r);
    if (r.error) return;
    if (!player_input_sane(in)) continue;
    queue_input(slot, in);
  }
}

static void remove_slot(Server& sv, int i) {
  if (!sv.clients[i].used) return;
  game_player_leave(sv.state, sv.clients[i].player_index);
  std::memset(&sv.clients[i], 0, sizeof(ClientSlot));
}

void server_pump(Server& sv, double now) {
  sv.now = now;
  uint8_t buf[MAX_PACKET];
  NetAddress from{};
  for (;;) {
    int got = udp_recv(sv.sock, buf, sizeof(buf), &from);
    if (got < 0) break;
    NetReader r;
    nr_init(r, buf, got);
    PacketType type{};
    if (!packet_header_read(r, &type)) continue;
    int slot = find_slot(sv, from);
    if (slot >= 0) sv.clients[slot].last_recv_time = now;
    if (type == PKT_CL_HELLO) {
      handle_hello(sv, from, r);
    } else if (type == PKT_CL_INPUT) {
      handle_input(sv, slot, r);
    } else if (type == PKT_CL_DISCONNECT) {
      if (slot >= 0) remove_slot(sv, slot);
    }
  }

  for (int i = 0; i < MAX_PLAYERS; ++i) {
    if (sv.clients[i].used && now - sv.clients[i].last_recv_time > CLIENT_TIMEOUT) {
      remove_slot(sv, i);
    }
  }
}

void server_tick(Server& sv) {
  PlayerInput inputs[MAX_PLAYERS]{};
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    ClientSlot& slot = sv.clients[i];
    if (!slot.used) continue;
    // After a burst (client hitch), skip to the two newest queued inputs so
    // queued latency cannot build up permanently.
    while (slot.input_queue_len > 2) {
      for (int k = 1; k < slot.input_queue_len; ++k) slot.input_queue[k - 1] = slot.input_queue[k];
      --slot.input_queue_len;
    }
    if (slot.input_queue_len > 0) {
      slot.latest_input = slot.input_queue[0];
      slot.highest_input_seq = slot.latest_input.sequence;
      for (int k = 1; k < slot.input_queue_len; ++k) slot.input_queue[k - 1] = slot.input_queue[k];
      --slot.input_queue_len;
    }
    PlayerInput in = slot.latest_input;
    if (sv.now - slot.last_recv_time > INPUT_STALE_TIME) {
      // Starved of packets: keep aim but stop running/firing with ghost input.
      in.buttons = 0;
      in.weapon_switch = 0;
      in.class_switch = 0;
    }
    int p = slot.player_index;
    if (p >= 0 && p < MAX_PLAYERS) inputs[p] = in;
  }
  game_tick(sv.state, sv.map, inputs, sv.rng, &sv.history);
  // Record the post-tick poses for the next window of lag compensation.
  history_record(sv.history, sv.state);
}

void server_broadcast(Server& sv) {
  uint8_t buf[MAX_PACKET];
  NetWriter w;
  nw_init(w, buf, sizeof(buf));
  packet_header_write(w, PKT_SV_SNAPSHOT);
  snapshot_write(sv.state, w);
  if (w.overflow) {
    log_warn("snapshot overflow");
    return;
  }
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    if (sv.clients[i].used) udp_send(sv.sock, sv.clients[i].addr, buf, w.len);
  }
}

void server_shutdown(Server& sv) {
  if (sv.sock.valid) {
    uint8_t buf[MAX_PACKET];
    NetWriter w;
    nw_init(w, buf, sizeof(buf));
    packet_header_write(w, PKT_SV_SHUTDOWN);
    for (int i = 0; i < MAX_PLAYERS; ++i) {
      if (sv.clients[i].used) udp_send(sv.sock, sv.clients[i].addr, buf, w.len);
    }
    udp_close(sv.sock);
  }
  net_shutdown();
}

#include "game/snapshot.h"

#include "game/tuning.h"

#include <cstring>

static uint8_t player_flags(const Player& p) {
  uint8_t flags = 0;
  if (p.alive) flags |= 1;
  if (p.on_ground) flags |= 2;
  if (p.crouching) flags |= 4;
  if (p.sliding) flags |= 8;
  return flags;
}

void snapshot_write(const GameState& s, NetWriter& w) {
  nw_u32(w, s.tick);
  nw_u8(w, s.match_over ? 1 : 0);
  nw_f32(w, s.restart_timer);
  nw_u8(w, static_cast<uint8_t>(s.frag_limit));

  int player_count = 0;
  for (int i = 0; i < MAX_PLAYERS; ++i) if (s.players[i].active) ++player_count;
  nw_u8(w, static_cast<uint8_t>(player_count));
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    const Player& p = s.players[i];
    if (!p.active) continue;
    nw_u8(w, static_cast<uint8_t>(i));
    nw_u8(w, player_flags(p));
    nw_string(w, p.name, 15);
    nw_vec3(w, p.pos);
    nw_vec3(w, p.vel);
    nw_f32(w, p.yaw);
    nw_f32(w, p.pitch);
    nw_f32(w, p.health);
    nw_u8(w, p.weapon);
    nw_f32(w, p.fire_cooldown);
    nw_f32(w, p.dash_cooldown);
    nw_f32(w, p.respawn_timer);
    nw_u32(w, static_cast<uint32_t>(p.frags));
    nw_u32(w, p.last_input_seq);
  }

  int rocket_count = 0;
  for (int i = 0; i < MAX_ROCKETS; ++i) if (s.rockets[i].active && rocket_count < 32) ++rocket_count;
  nw_u8(w, static_cast<uint8_t>(rocket_count));
  int wrote = 0;
  for (int i = 0; i < MAX_ROCKETS && wrote < 32; ++i) {
    const Rocket& r = s.rockets[i];
    if (!r.active) continue;
    nw_u8(w, static_cast<uint8_t>(i));
    nw_vec3(w, r.pos);
    nw_vec3(w, r.vel);
    nw_u8(w, r.owner);
    ++wrote;
  }

  nw_u8(w, static_cast<uint8_t>(s.pickup_count));
  for (int i = 0; i < s.pickup_count; ++i) {
    const Pickup& p = s.pickups[i];
    nw_u8(w, p.present ? 1 : 0);
    nw_vec3(w, p.pos);
    nw_f32(w, p.respawn_timer);
  }

  GameEvent newest[16]{};
  int event_count = 0;
  uint32_t min_tick = s.tick > 15 ? s.tick - 15 : 0;
  for (int i = 0; i < MAX_EVENTS; ++i) {
    const GameEvent& e = s.events[i];
    if (e.id == 0 || e.tick < min_tick) continue;
    if (event_count < 16) newest[event_count++] = e;
  }
  nw_u8(w, static_cast<uint8_t>(event_count));
  for (int i = 0; i < event_count; ++i) {
    const GameEvent& e = newest[i];
    nw_u32(w, e.id);
    nw_u8(w, e.type);
    nw_u8(w, e.a);
    nw_u8(w, e.b);
    nw_vec3(w, e.pos);
    nw_u32(w, e.tick);
  }
}

bool snapshot_read(GameState& s, NetReader& r) {
  std::memset(&s, 0, sizeof(s));
  s.tick = nr_u32(r);
  s.match_over = nr_u8(r) != 0;
  s.restart_timer = nr_f32(r);
  s.frag_limit = nr_u8(r);

  int player_count = nr_u8(r);
  if (player_count > MAX_PLAYERS) r.error = true;
  for (int n = 0; n < player_count && !r.error; ++n) {
    int i = nr_u8(r);
    if (i < 0 || i >= MAX_PLAYERS) {
      r.error = true;
      break;
    }
    Player& p = s.players[i];
    p.active = true;
    uint8_t flags = nr_u8(r);
    p.alive = (flags & 1) != 0;
    p.on_ground = (flags & 2) != 0;
    p.crouching = (flags & 4) != 0;
    p.sliding = (flags & 8) != 0;
    nr_string(r, p.name, sizeof(p.name));
    p.pos = nr_vec3(r);
    p.vel = nr_vec3(r);
    p.yaw = nr_f32(r);
    p.pitch = nr_f32(r);
    p.health = nr_f32(r);
    p.weapon = nr_u8(r);
    p.fire_cooldown = nr_f32(r);
    p.dash_cooldown = nr_f32(r);
    p.respawn_timer = nr_f32(r);
    p.frags = static_cast<int>(nr_u32(r));
    p.last_input_seq = nr_u32(r);
  }

  int rocket_count = nr_u8(r);
  if (rocket_count > 32) r.error = true;
  for (int n = 0; n < rocket_count && !r.error; ++n) {
    int i = nr_u8(r);
    if (i < 0 || i >= MAX_ROCKETS) {
      r.error = true;
      break;
    }
    Rocket& ro = s.rockets[i];
    ro.active = true;
    ro.pos = nr_vec3(r);
    ro.vel = nr_vec3(r);
    ro.owner = nr_u8(r);
  }

  s.pickup_count = nr_u8(r);
  if (s.pickup_count > MAX_PICKUPS) r.error = true;
  for (int i = 0; i < s.pickup_count && !r.error; ++i) {
    Pickup& p = s.pickups[i];
    p.present = nr_u8(r) != 0;
    p.pos = nr_vec3(r);
    p.respawn_timer = nr_f32(r);
  }

  int event_count = nr_u8(r);
  if (event_count > 16) r.error = true;
  for (int n = 0; n < event_count && !r.error; ++n) {
    GameEvent e{};
    e.id = nr_u32(r);
    e.type = nr_u8(r);
    e.a = nr_u8(r);
    e.b = nr_u8(r);
    e.pos = nr_vec3(r);
    e.tick = nr_u32(r);
    s.events[e.id % MAX_EVENTS] = e;
    if (e.id >= s.next_event_id) s.next_event_id = e.id + 1;
  }

  return !r.error;
}

void snapshot_interpolate(const GameState& a, const GameState& b, float t, GameState& out) {
  out = b;
  t = clampf(t, 0.0f, 1.0f);
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    if (!a.players[i].active || !b.players[i].active) continue;
    float dist = vec3_length(b.players[i].pos - a.players[i].pos);
    if (dist <= 5.0f) {
      out.players[i].pos = vec3_lerp(a.players[i].pos, b.players[i].pos, t);
      out.players[i].yaw = angle_lerp(a.players[i].yaw, b.players[i].yaw, t);
      out.players[i].pitch = angle_lerp(a.players[i].pitch, b.players[i].pitch, t);
    }
  }
  for (int i = 0; i < MAX_ROCKETS; ++i) {
    if (!a.rockets[i].active || !b.rockets[i].active) continue;
    float dist = vec3_length(b.rockets[i].pos - a.rockets[i].pos);
    if (dist <= 5.0f) out.rockets[i].pos = vec3_lerp(a.rockets[i].pos, b.rockets[i].pos, t);
  }
}

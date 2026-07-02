#include "game/sim.h"

#include "game/movement.h"
#include "game/tuning.h"
#include "game/weapons.h"

#include <cmath>
#include <cstdio>
#include <cstring>

void push_event(GameState& s, uint8_t type, uint8_t a, uint8_t b, Vec3 pos) {
  uint32_t id = s.next_event_id++;
  if (id == 0) id = s.next_event_id++;
  GameEvent& e = s.events[id % MAX_EVENTS];
  e.id = id;
  e.type = type;
  e.a = a;
  e.b = b;
  e.pos = pos;
  e.tick = s.tick;
}

Vec3 pick_spawn(const GameState& s, const Map& map, Rng& rng, float* yaw_out) {
  if (map.spawn_count <= 0) {
    if (yaw_out) *yaw_out = 0.0f;
    return {0.0f, 2.0f, 0.0f};
  }

  float best_score = -1.0f;
  int best_indices[MAX_SPAWNS]{};
  int best_count = 0;
  for (int i = 0; i < map.spawn_count; ++i) {
    float nearest = 1000000.0f;
    for (int p = 0; p < MAX_PLAYERS; ++p) {
      const Player& other = s.players[p];
      if (!other.active || !other.alive) continue;
      float d = vec3_length(map.spawns[i] - other.pos);
      if (d < nearest) nearest = d;
    }
    if (nearest > best_score + 0.001f) {
      best_score = nearest;
      best_count = 0;
      best_indices[best_count++] = i;
    } else if (std::fabs(nearest - best_score) <= 0.001f && best_count < MAX_SPAWNS) {
      best_indices[best_count++] = i;
    }
  }

  int chosen = best_indices[best_count > 1 ? rng_int(rng, 0, best_count - 1) : 0];
  if (yaw_out) *yaw_out = map.spawn_yaws[chosen];
  return map.spawns[chosen];
}

void game_init(GameState& s, const Map& map, int frag_limit) {
  std::memset(&s, 0, sizeof(s));
  s.next_event_id = 1;
  s.frag_limit = frag_limit > 0 ? frag_limit : DEFAULT_FRAG_LIMIT;
  s.pickup_count = map.health_count < MAX_PICKUPS ? map.health_count : MAX_PICKUPS;
  for (int i = 0; i < s.pickup_count; ++i) {
    s.pickups[i].present = true;
    s.pickups[i].pos = map.health_spawns[i];
  }
}

void game_set_player_class(Player& p, uint8_t player_class) {
  if (player_class >= PLAYER_CLASS_COUNT) player_class = CLASS_RANGER;
  float old_max = player_class_max_health(p.player_class);
  float new_max = player_class_max_health(player_class);
  float health_frac = old_max > 0.0f ? clampf(p.health / old_max, 0.0f, 1.0f) : 1.0f;
  p.player_class = player_class;
  p.health = new_max * health_frac;
  if (!p.alive) p.health = 0.0f;
  p.weapon = player_class_primary_weapon(p.player_class);
}

int game_player_join(GameState& s, const Map& map, const char* name) {
  Rng rng{0x1234abcdull + s.tick + s.next_event_id};
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    Player& p = s.players[i];
    if (p.active) continue;
    std::memset(&p, 0, sizeof(p));
    p.active = true;
    p.alive = true;
    std::snprintf(p.name, sizeof(p.name), "%s", (name && name[0]) ? name : "player");
    p.player_class = CLASS_RANGER;
    p.pos = pick_spawn(s, map, rng, &p.yaw);
    p.pitch = 0.0f;
    p.health = player_class_max_health(p.player_class);
    p.weapon = player_class_primary_weapon(p.player_class);
    p.stamina = MAX_STAMINA;
    p.on_ground = false;
    push_event(s, EV_JOIN, static_cast<uint8_t>(i), 0, p.pos);
    return i;
  }
  return -1;
}

void game_player_leave(GameState& s, int player_index) {
  if (player_index < 0 || player_index >= MAX_PLAYERS) return;
  if (!s.players[player_index].active) return;
  Vec3 pos = s.players[player_index].pos;
  push_event(s, EV_LEAVE, static_cast<uint8_t>(player_index), 0, pos);
  std::memset(&s.players[player_index], 0, sizeof(Player));
}

static void respawn_player(GameState& s, const Map& map, int i, Rng& rng) {
  Player& p = s.players[i];
  p.alive = true;
  p.health = player_class_max_health(p.player_class);
  p.vel = {0, 0, 0};
  p.pos = pick_spawn(s, map, rng, &p.yaw);
  p.pitch = 0.0f;
  p.on_ground = false;
  p.crouching = false;
  p.sliding = false;
  p.weapon = player_class_primary_weapon(p.player_class);
  p.fire_cooldown = 0.0f;
  p.dash_cooldown = 0.0f;
  p.slide_time = 0.0f;
  p.jump_buffer = 0.0f;
  p.stamina = MAX_STAMINA;
  p.stamina_recharge_timer = 0.0f;
  p.wall_normal = {0, 0, 0};
  p.wall_contact_time = 0.0f;
  p.wall_jump_cooldown = 0.0f;
  p.dash_air_control_time = 0.0f;
  p.jump_held = false;
  p.dash_held = false;
  p.air_jump_used = false;
  push_event(s, EV_SOUND, SND_RESPAWN, static_cast<uint8_t>(i), p.pos);
}

void game_tick(GameState& s, const Map& map, const PlayerInput inputs[MAX_PLAYERS], Rng& rng) {
  s.tick += 1;

  if (s.match_over) {
    s.restart_timer -= TICK_DT;
    if (s.restart_timer <= 0.0f) {
      for (int i = 0; i < MAX_PLAYERS; ++i) {
        if (!s.players[i].active) continue;
        s.players[i].frags = 0;
        respawn_player(s, map, i, rng);
      }
      for (int i = 0; i < MAX_ROCKETS; ++i) s.rockets[i].active = false;
      s.match_over = false;
      s.restart_timer = 0.0f;
    }
    return;
  }

  for (int i = 0; i < s.pickup_count; ++i) {
    Pickup& pk = s.pickups[i];
    if (!pk.present) {
      pk.respawn_timer -= TICK_DT;
      if (pk.respawn_timer <= 0.0f) {
        pk.present = true;
        pk.respawn_timer = 0.0f;
      }
    }
  }

  for (int i = 0; i < MAX_PLAYERS; ++i) {
    Player& p = s.players[i];
    if (!p.active) continue;
    if (p.fire_cooldown > 0.0f) p.fire_cooldown -= TICK_DT;
    if (!p.alive) {
      p.respawn_timer -= TICK_DT;
      if (p.respawn_timer <= 0.0f) respawn_player(s, map, i, rng);
      continue;
    }

    PlayerInput in = inputs[i];
    if (in.class_switch >= 1 && in.class_switch <= PLAYER_CLASS_COUNT) {
      uint8_t requested_class = player_class_from_switch(in.class_switch);
      if (p.player_class != requested_class) game_set_player_class(p, requested_class);
    }
    if (in.weapon_switch == 1) p.weapon = player_class_primary_weapon(p.player_class);
    if (in.weapon_switch == 2) p.weapon = WEAPON_ROCKET;
    if (in.sequence >= p.last_input_seq) p.last_input_seq = in.sequence;

    bool old_ground = p.on_ground;
    bool old_sliding = p.sliding;
    player_move(p, in, map, TICK_DT);
    if (!old_ground && p.on_ground) {
      push_event(s, EV_SOUND, SND_JUMP, static_cast<uint8_t>(i), p.pos);
    }
    if (!old_sliding && p.sliding) {
      push_event(s, EV_SOUND, SND_SLIDE, static_cast<uint8_t>(i), p.pos);
    }
    if (in.buttons & BTN_FIRE) {
      weapon_fire(s, map, i);
    }

    for (int k = 0; k < s.pickup_count; ++k) {
      Pickup& pk = s.pickups[k];
      float max_health = player_class_max_health(p.player_class);
      if (!pk.present || p.health >= max_health) continue;
      if (vec3_length(pk.pos - p.pos) < 1.0f) {
        p.health = clampf(p.health + HEALTH_PACK_AMOUNT, 0.0f, max_health);
        pk.present = false;
        pk.respawn_timer = HEALTH_RESPAWN_TIME;
        push_event(s, EV_SOUND, SND_PICKUP, static_cast<uint8_t>(i), pk.pos);
      }
    }
  }

  rockets_tick(s, map, TICK_DT);

  for (int i = 0; i < MAX_PLAYERS; ++i) {
    if (s.players[i].active && s.players[i].frags >= s.frag_limit) {
      s.match_over = true;
      s.restart_timer = MATCH_RESTART_TIME;
      break;
    }
  }
}

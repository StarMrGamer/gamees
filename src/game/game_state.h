#pragma once

#include "core/math.h"
#include "game/tuning.h"

#include <cmath>
#include <cstdint>

enum Buttons : uint16_t {
  BTN_FORWARD = 1,
  BTN_BACK = 2,
  BTN_LEFT = 4,
  BTN_RIGHT = 8,
  BTN_JUMP = 16,
  BTN_CROUCH = 32,
  BTN_FIRE = 64,
  BTN_DASH = 128,
  BTN_AIRJUMP = 256,
  // Held key state, not an edge: the simulation edge-detects it so the toggle
  // happens exactly once however many ticks the key is down for.
  BTN_NOCLIP = 512,
};

enum Weapon : uint8_t {
  WEAPON_RIFLE = 0,
  WEAPON_ROCKET = 1,
  WEAPON_SHOTGUN = 2,
  WEAPON_LMG = 3,
  WEAPON_SNIPER = 4,
};

enum PlayerClass : uint8_t {
  CLASS_RANGER = 0,
  CLASS_SCOUT = 1,
  CLASS_TANK = 2,
  CLASS_SNIPER = 3,
  PLAYER_CLASS_COUNT = 4,
};

enum EventType : uint8_t {
  EV_KILL = 1,
  EV_SOUND,
  EV_JOIN,
  EV_LEAVE,
  EV_HIT,
};

enum SoundId : uint8_t {
  SND_RIFLE = 1,
  SND_ROCKET_LAUNCH,
  SND_EXPLOSION,
  SND_JUMP,
  SND_DASH,
  SND_SLIDE,
  SND_PICKUP,
  SND_HURT,
  SND_DEATH,
  SND_RESPAWN,
  SND_LAND,
  SND_SHOTGUN,
  SND_LMG,
  SND_SNIPER,
  SND_COUNT,
};

struct PlayerInput {
  uint32_t sequence;
  uint16_t buttons;
  uint8_t weapon_switch;
  uint8_t class_switch;
  float yaw, pitch;
  // Server tick the client's view was rendering when this input was sampled.
  // The server rewinds remote hit boxes to this tick for lag compensation.
  // Zero means "unknown", disabling rewind for the input.
  uint32_t view_tick;
};

inline bool player_input_sane(const PlayerInput& in) {
  return std::isfinite(in.yaw) && std::isfinite(in.pitch);
}

inline uint8_t player_class_from_switch(uint8_t switch_value) {
  return switch_value >= 1 && switch_value <= PLAYER_CLASS_COUNT
    ? static_cast<uint8_t>(switch_value - 1)
    : static_cast<uint8_t>(CLASS_RANGER);
}

inline const char* player_class_name(uint8_t player_class) {
  switch (player_class) {
    case CLASS_SCOUT: return "SCOUT";
    case CLASS_TANK: return "TANK";
    default: return "RANGER";
  }
}

inline uint8_t player_class_primary_weapon(uint8_t player_class) {
  switch (player_class) {
    case CLASS_SCOUT: return WEAPON_SHOTGUN;
    case CLASS_TANK: return WEAPON_LMG;
    case CLASS_SNIPER: return WEAPON_SNIPER;
    default: return WEAPON_RIFLE;
  }
}

inline float player_class_max_health(uint8_t player_class) {
  switch (player_class) {
    case CLASS_SCOUT: return 85.0f;
    case CLASS_TANK: return 125.0f;
    case CLASS_SNIPER: return 80.0f;  // the most fragile class, by design
    default: return PLAYER_MAX_HEALTH;
  }
}

inline float player_class_speed_scale(uint8_t player_class) {
  switch (player_class) {
    case CLASS_SCOUT: return 1.12f;
    case CLASS_TANK: return 0.88f;
    case CLASS_SNIPER: return 0.92f;
    default: return 1.0f;
  }
}

inline float player_class_dash_impulse_scale(uint8_t player_class) {
  switch (player_class) {
    case CLASS_SCOUT: return 1.10f;
    case CLASS_TANK: return 0.90f;
    case CLASS_SNIPER: return 1.00f;
    default: return 1.0f;
  }
}

inline float player_class_dash_cooldown_scale(uint8_t player_class) {
  switch (player_class) {
    case CLASS_SCOUT: return 0.80f;
    case CLASS_TANK: return 1.20f;
    case CLASS_SNIPER: return 1.15f;
    default: return 1.0f;
  }
}

inline float player_class_damage_scale(uint8_t player_class) {
  switch (player_class) {
    case CLASS_SCOUT: return 0.94f;
    case CLASS_TANK: return 1.04f;
    case CLASS_SNIPER: return 1.00f;
    default: return 1.0f;
  }
}

inline float player_class_damage_taken_scale(uint8_t player_class) {
  switch (player_class) {
    case CLASS_SCOUT: return 1.05f;
    case CLASS_TANK: return 0.92f;
    case CLASS_SNIPER: return 1.08f;  // punished hardest for being caught close
    default: return 1.0f;
  }
}

struct Player {
  bool active;
  bool alive;
  char name[16];
  Vec3 pos, vel;
  float yaw, pitch;
  float health;
  uint8_t weapon;
  uint8_t player_class;
  bool on_ground, crouching, sliding;
  float fire_cooldown, dash_cooldown, slide_time, respawn_timer, jump_buffer;
  int stamina;
  float stamina_recharge_timer;
  Vec3 wall_normal;
  float wall_contact_time, wall_jump_cooldown, dash_air_control_time;
  float air_jump_buffer;
  bool jump_held, dash_held, air_jump_held, air_jump_used, slide_suppressed;
  bool noclip, noclip_held;
  uint8_t move_sound;
  int frags;
  uint32_t last_input_seq;
};

struct Rocket {
  bool active;
  Vec3 pos, vel;
  uint8_t owner;
  float life;
};

struct Pickup {
  bool present;
  Vec3 pos;
  float respawn_timer;
};

struct GameEvent {
  uint32_t id;
  uint8_t type;
  uint8_t a, b;
  Vec3 pos;
  uint32_t tick;
};

struct GameState {
  uint32_t tick;
  Player players[MAX_PLAYERS];
  Rocket rockets[MAX_ROCKETS];
  Pickup pickups[MAX_PICKUPS];
  int pickup_count;
  GameEvent events[MAX_EVENTS];
  uint32_t next_event_id;
  int frag_limit;
  bool match_over;
  float restart_timer;
};

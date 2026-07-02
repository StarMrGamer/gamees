#pragma once

#include "core/math.h"
#include "game/tuning.h"

#include <cstdint>

enum Buttons : uint8_t {
  BTN_FORWARD = 1,
  BTN_BACK = 2,
  BTN_LEFT = 4,
  BTN_RIGHT = 8,
  BTN_JUMP = 16,
  BTN_CROUCH = 32,
  BTN_FIRE = 64,
  BTN_DASH = 128,
};

enum Weapon : uint8_t {
  WEAPON_RIFLE = 0,
  WEAPON_ROCKET = 1,
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
  SND_COUNT,
};

struct PlayerInput {
  uint32_t sequence;
  uint8_t buttons;
  uint8_t weapon_switch;
  float yaw, pitch;
};

struct Player {
  bool active;
  bool alive;
  char name[16];
  Vec3 pos, vel;
  float yaw, pitch;
  float health;
  uint8_t weapon;
  bool on_ground, crouching, sliding;
  float fire_cooldown, dash_cooldown, slide_time, respawn_timer, jump_buffer;
  bool jump_held;
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

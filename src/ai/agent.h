#pragma once

#include "core/rng.h"
#include "game/game_state.h"
#include "game/map.h"

// Bot brains. This produces a PlayerInput from a GameState and nothing else -
// no sockets, no rendering, no wall clock - so the same code drives a live
// networked bot, a headless evaluation match, and (later) the demonstrations a
// learned policy trains against.
enum AgentKind : uint8_t {
  // The original bot: hold forward and fire, sweep the yaw on a sine, never
  // aim. Kept as the floor to measure against.
  AGENT_SIMPLE = 0,
  AGENT_DEMON = 1,
};

// Everything the bot has to remember between ticks. The simulation itself is
// stateless per tick, so this is the bot's only continuity.
struct AgentMemory {
  int target;                // player index being fought, -1 for none
  float target_lock;         // seconds this target has been held
  float seen_for;            // seconds of unbroken line of sight
  float lost_for;            // seconds since sight was last broken
  Vec3 last_known;           // where the target was last seen
  bool has_last_known;
  float aim_yaw, aim_pitch;  // the bot's own aim, swept toward the target
  bool aim_init;
  float strafe_sign;         // which way it is currently circling
  float strafe_timer;
  float wander_yaw;
  float wander_timer;
  float jump_timer;
  float clock;               // seconds lived, for the simple bot's sine
};

// Skill knobs. Separating them from the code is what makes a difficulty slider
// possible later, and what lets an evaluation sweep find where a learned policy
// actually sits rather than just "beats the bot".
struct AgentConfig {
  float turn_rate;        // rad/s the aim may sweep - the main skill dial
  float reaction;         // seconds of sight before it will shoot
  float aim_error;        // radians of persistent aim offset
  float fire_cone;        // radians of aim error it will still fire inside
  float strafe_min, strafe_max;  // seconds between direction flips
  float seek_health_below;       // health fraction that sends it to a pickup
};

AgentConfig agent_config_demon();
// A deliberately worse demon, for difficulty and for sweeping a learned policy
// against a ladder rather than a single opponent. 0 = full strength.
AgentConfig agent_config_demon_handicapped(float handicap);

void agent_reset(AgentMemory& m);

PlayerInput agent_think(const GameState& s, const Map& map, int self, AgentKind kind,
                        const AgentConfig& cfg, AgentMemory& mem, Rng& rng, float dt);

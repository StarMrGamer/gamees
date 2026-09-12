#pragma once

#include "core/rng.h"
#include "game/game_state.h"
#include "game/map.h"
#include "game/nav.h"

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

  // Navigation. A route is followed for a while rather than recomputed every
  // tick: an A* query is ~10 us and the answer barely changes at 60 Hz.
  // Buttons the engine edge-triggers (jump, dash, air jump) have to be
  // released before they can fire again, so the bot tracks what it held.
  bool held_jump, held_dash, held_airjump;
  // Correlated aim error: a slow wander rather than per-tick noise.
  float drift_yaw, drift_pitch;
  // Whoever last put a bullet in us, and for how long that still counts as a
  // reason to look round. Being shot from behind is information a human gets.
  int provoked_by;
  float provoked_time;
  uint32_t last_event_seen;

  NavPath path;
  int path_index;
  float repath_timer;
  Vec3 path_goal;            // what the current route was planned toward
  bool has_path;
};

// Skill knobs. Separating them from the code is what makes a difficulty slider
// possible, and what lets an evaluation sweep find where a learned policy
// actually sits rather than just "beats the bot".
struct AgentConfig {
  float turn_rate;        // rad/s the aim may sweep - the main skill dial
  float reaction;         // seconds of sight before it will shoot
  float aim_error;        // radians of aim wander (see aim_drift_rate)
  // How fast that wander moves. Per-tick random jitter is the wrong model: it
  // averages out over a burst, so a bot with "error" still lands almost every
  // shot. Real misses come from an offset that persists for a moment, so the
  // error is a slow drift instead.
  float aim_drift_rate;
  float fire_cone;        // radians of aim error it will still fire inside
  // Half-angle of the cone the bot can acquire targets in. Without this it
  // raycasts from its eye in every direction at once and cannot be flanked,
  // which is most of what made it unbeatable.
  float fov;
  float strafe_min, strafe_max;  // seconds between direction flips
  float seek_health_below;       // health fraction that sends it to a pickup
};

// Player-facing difficulty. DEMON is the original full-strength bot, kept as
// the top tier so earlier measurements stay comparable; it is not meant to be
// fair.
enum AgentSkill : uint8_t {
  SKILL_EASY = 0,
  SKILL_NORMAL,
  SKILL_HARD,
  SKILL_DEMON,
  SKILL_COUNT,
};

AgentConfig agent_config(AgentSkill skill);
const char* agent_skill_name(AgentSkill skill);
bool agent_skill_parse(const char* name, AgentSkill* out);

AgentConfig agent_config_demon();
// A deliberately worse demon, for difficulty and for sweeping a learned policy
// against a ladder rather than a single opponent. 0 = full strength.
AgentConfig agent_config_demon_handicapped(float handicap);

void agent_reset(AgentMemory& m);

PlayerInput agent_think(const GameState& s, const Map& map, int self, AgentKind kind,
                        const AgentConfig& cfg, AgentMemory& mem, Rng& rng, float dt);

#include "ai/agent.h"
#include "core/rng.h"
#include "game/collision.h"
#include "game/sim.h"
#include "game/tuning.h"
#include "test_harness.h"

#include <cmath>
#include <memory>
#include <string>

namespace {

// A small sealed box with a couple of pillars: big enough that the bots have
// to move and take cover, small enough that a test runs in a blink.
std::string duel_map() {
  return "name duel\n"
         "box -20 -1 -20 40 1 40 0.5 0.5 0.5\n"
         "box -20 0 -20 40 6 1 0.4 0.4 0.4\n"
         "box -20 0 19 40 6 1 0.4 0.4 0.4\n"
         "box -20 0 -20 1 6 40 0.4 0.4 0.4\n"
         "box 19 0 -20 1 6 40 0.4 0.4 0.4\n"
         "box -4 0 -4 2 4 2 0.6 0.3 0.3\n"
         "box 4 0 4 2 4 2 0.6 0.3 0.3\n"
         "spawn -14 1 -14 45\n"
         "spawn 14 1 14 225\n";
}

// Runs a duel and returns each side's frags. Player 0 is `a`, player 1 is `b`.
void duel(AgentKind a, AgentKind b, int ticks, int* frags_a, int* frags_b,
          bool* all_inputs_sane) {
  auto map = std::make_unique<Map>();
  std::string text = duel_map();
  CHECK(map_parse(text.c_str(), map.get()));
  auto st = std::make_unique<GameState>();
  Rng rng{0x5150abcdull};
  game_init(*st, *map, 999);
  game_player_join(*st, *map, "a");
  game_player_join(*st, *map, "b");

  AgentMemory mem[2];
  agent_reset(mem[0]);
  agent_reset(mem[1]);
  const AgentConfig cfg = agent_config_demon();
  bool sane = true;

  for (int t = 0; t < ticks; ++t) {
    PlayerInput in[MAX_PLAYERS]{};
    for (int i = 0; i < 2; ++i) {
      in[i] = agent_think(*st, *map, i, i == 0 ? a : b, cfg, mem[i], rng, TICK_DT);
      in[i].sequence = static_cast<uint32_t>(t + 1);
      if (!player_input_sane(in[i])) sane = false;
    }
    game_tick(*st, *map, in, rng);
  }
  *frags_a = st->players[0].frags;
  *frags_b = st->players[1].frags;
  *all_inputs_sane = sane;
}

}  // namespace

// The point of the demon is that it is better than what came before. If this
// ever stops being true by a wide margin, the bot regressed.
TEST(demon_beats_the_simple_bot) {
  int a = 0;
  int b = 0;
  bool sane = false;
  duel(AGENT_DEMON, AGENT_SIMPLE, 60 * 40, &a, &b, &sane);
  CHECK(sane);
  CHECK(a > b);
  CHECK(a >= 5);  // it should be landing kills, not just edging ahead
}

// Swapping seats must not swap the result: if it does, the harness is
// measuring the spawn points rather than the agents.
TEST(demon_wins_from_either_seat) {
  int a = 0;
  int b = 0;
  bool sane = false;
  duel(AGENT_SIMPLE, AGENT_DEMON, 60 * 40, &a, &b, &sane);
  CHECK(sane);
  CHECK(b > a);
}

// Every field the simulation reads has to be finite and in range, on every
// tick, including the ticks where the bot is dead or has no target.
TEST(agent_inputs_stay_in_range) {
  auto map = std::make_unique<Map>();
  std::string text = duel_map();
  CHECK(map_parse(text.c_str(), map.get()));
  auto st = std::make_unique<GameState>();
  Rng rng{0x99ull};
  game_init(*st, *map, 999);
  game_player_join(*st, *map, "a");
  game_player_join(*st, *map, "b");
  AgentMemory mem[2];
  agent_reset(mem[0]);
  agent_reset(mem[1]);
  const AgentConfig cfg = agent_config_demon();

  int dead_ticks = 0;
  for (int t = 0; t < 60 * 60; ++t) {
    PlayerInput in[MAX_PLAYERS]{};
    for (int i = 0; i < 2; ++i) {
      in[i] = agent_think(*st, *map, i, AGENT_DEMON, cfg, mem[i], rng, TICK_DT);
      in[i].sequence = static_cast<uint32_t>(t + 1);
      CHECK(player_input_sane(in[i]));
      CHECK(in[i].pitch >= -1.5f && in[i].pitch <= 1.5f);
      CHECK(in[i].yaw >= -PI - 0.001f && in[i].yaw <= PI + 0.001f);
      CHECK(in[i].weapon_switch <= 2);
      if (!st->players[i].alive) ++dead_ticks;
    }
    game_tick(*st, *map, in, rng);
  }
  // Guard against a vacuous pass: the run has to have covered the dead path.
  CHECK(dead_ticks > 0);
}

// The bot must not shoot at what it cannot see. Two bots either side of a wall
// with no line of sight should never pull the trigger.
TEST(agent_holds_fire_without_line_of_sight) {
  auto map = std::make_unique<Map>();
  // One long wall straight down the middle, taller than anyone can see over.
  CHECK(map_parse("name split\n"
                  "box -20 -1 -20 40 1 40 0.5 0.5 0.5\n"
                  "box -0.5 0 -20 1 10 40 0.4 0.4 0.4\n"
                  "spawn -10 1 0 90\n"
                  "spawn 10 1 0 270\n",
                  map.get()));
  auto st = std::make_unique<GameState>();
  Rng rng{0x77ull};
  game_init(*st, *map, 999);
  game_player_join(*st, *map, "a");
  game_player_join(*st, *map, "b");
  // Pin them either side of the wall so neither can ever see the other.
  st->players[0].pos = {-10.0f, 0.0f, 0.0f};
  st->players[1].pos = {10.0f, 0.0f, 0.0f};

  AgentMemory mem;
  agent_reset(mem);
  const AgentConfig cfg = agent_config_demon();
  int shots = 0;
  for (int t = 0; t < 60 * 5; ++t) {
    st->players[0].pos = {-10.0f, 0.0f, 0.0f};  // hold it there
    st->players[1].pos = {10.0f, 0.0f, 0.0f};
    PlayerInput in = agent_think(*st, *map, 0, AGENT_DEMON, cfg, mem, rng, TICK_DT);
    if (in.buttons & BTN_FIRE) ++shots;
  }
  CHECK_EQ_INT(shots, 0);
  // And it should still be trying to get there, not standing idle.
  CHECK(!map_box_overlap(*map, player_aabb(st->players[0].pos, false)));
}

// The handicap dial has to actually weaken the bot, or a difficulty setting and
// any ladder built on it are decoration.
TEST(handicap_weakens_the_agent) {
  AgentConfig full = agent_config_demon();
  AgentConfig none = agent_config_demon_handicapped(0.0f);
  AgentConfig worst = agent_config_demon_handicapped(1.0f);
  // Zero handicap must be exactly the full-strength config, otherwise a
  // mirror match is not actually a mirror.
  CHECK_NEAR(none.turn_rate, full.turn_rate, 0.0001f);
  CHECK_NEAR(none.aim_error, full.aim_error, 0.0001f);
  CHECK(worst.turn_rate < full.turn_rate);
  CHECK(worst.reaction > full.reaction);
  CHECK(worst.aim_error > full.aim_error);
  // And it should be monotonic in between, not just at the ends.
  float prev_turn = full.turn_rate;
  for (float h = 0.2f; h <= 1.0f; h += 0.2f) {
    AgentConfig c = agent_config_demon_handicapped(h);
    CHECK(c.turn_rate <= prev_turn);
    prev_turn = c.turn_rate;
  }
}

// A reset agent must not carry a stale target into its next life.
TEST(agent_reset_clears_target) {
  AgentMemory m;
  agent_reset(m);
  CHECK_EQ_INT(m.target, -1);
  CHECK(!m.aim_init);
  CHECK(!m.has_last_known);
}

// The bot must actually use the engine's movement tech, not just walk. Ground
// speed is capped at GROUND_MAX_SPEED, so sustained travel above it can only
// come from dashes, slides and slide-jumps.
TEST(demon_uses_movement_tech_to_exceed_walking_speed) {
  auto map = std::make_unique<Map>();
  // A long open corridor with a target at the far end, so the bot commits to
  // travelling rather than circling a nearby enemy.
  CHECK(map_parse("name runway\n"
                  "box -6 -1 -120 12 1 240 0.5 0.5 0.5\n"
                  "box -7 0 -120 1 8 240 0.4 0.4 0.4\n"
                  "box 6 0 -120 1 8 240 0.4 0.4 0.4\n"
                  "spawn 0 1 -100 0\n"
                  "spawn 0 1 100 180\n",
                  map.get()));
  map_build_nav(map.get());
  auto st = std::make_unique<GameState>();
  Rng rng{0x2468ull};
  game_init(*st, *map, 999);
  game_player_join(*st, *map, "a");
  game_player_join(*st, *map, "b");
  st->players[0].pos = {0.0f, 0.0f, -100.0f};
  st->players[1].pos = {0.0f, 0.0f, 100.0f};

  AgentMemory mem;
  agent_reset(mem);
  const AgentConfig cfg = agent_config_demon();
  float top = 0.0f;
  int ticks_above_walk = 0;
  int saw_dash = 0;
  int saw_slide = 0;
  for (int t = 0; t < 60 * 12; ++t) {
    PlayerInput in[MAX_PLAYERS]{};
    in[0] = agent_think(*st, *map, 0, AGENT_DEMON, cfg, mem, rng, TICK_DT);
    in[0].sequence = static_cast<uint32_t>(t + 1);
    if (in[0].buttons & BTN_DASH) ++saw_dash;
    in[1].sequence = static_cast<uint32_t>(t + 1);
    in[1].yaw = st->players[1].yaw;
    game_tick(*st, *map, in, rng);
    const Player& p = st->players[0];
    if (p.sliding) ++saw_slide;
    float sp = std::sqrt(p.vel.x * p.vel.x + p.vel.z * p.vel.z);
    if (sp > top) top = sp;
    if (sp > GROUND_MAX_SPEED + 0.5f) ++ticks_above_walk;
  }
  CHECK(saw_dash > 0);
  CHECK(saw_slide > 0);
  CHECK(top > GROUND_MAX_SPEED + 3.0f);
  // And it must hold that speed rather than touching it once.
  CHECK(ticks_above_walk > 60);
}

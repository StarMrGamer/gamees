#include "client/bot.h"

#include "ai/agent.h"
#include "client/client.h"
#include "core/log.h"
#include "core/rng.h"
#include "game/game_state.h"
#include "game/tuning.h"

#include <chrono>
#include <cmath>
#include <memory>
#include <thread>

static double bot_now() {
  using Clock = std::chrono::steady_clock;
  static Clock::time_point start = Clock::now();
  return std::chrono::duration<double>(Clock::now() - start).count();
}

int bot_main(NetAddress server, const char* name, int lifetime_seconds, int skill) {
  auto client = std::make_unique<Client>();
  Client& c = *client;
  if (!client_start(c, server, name ? name : "bot")) return 1;

  auto view = std::make_unique<GameState>();
  AgentMemory mem{};
  agent_reset(mem);
  const AgentConfig cfg = agent_config(static_cast<AgentSkill>(skill));
  log_info("bot '%s' at %s difficulty", name ? name : "bot",
           agent_skill_name(static_cast<AgentSkill>(skill)));
  Rng rng{0x1234abcdull ^ static_cast<uint64_t>(server.port)};

  double start = bot_now();
  double last = start;
  uint32_t sequence = 0;
  bool nav_ready = false;
  while (lifetime_seconds <= 0 || bot_now() - start < lifetime_seconds) {
    double now = bot_now();
    float dt = static_cast<float>(now - last);
    last = now;
    if (dt > 0.25f) dt = 0.25f;  // a stall must not teleport the bot's timers

    ClientEvents events{};
    client_receive(c, now, &events);

    PlayerInput in{};
    // The agent needs a world to reason about. That is exactly what the
    // client's prediction map plus its interpolated view provide, so a network
    // bot thinks from the same picture a human player is looking at - and
    // falls back to walking forward if the server's map is not available
    // locally, which is the same condition that disables prediction.
    if (c.prediction_ready && !nav_ready) {
      // The client loads the map for prediction; the bot needs a navmesh on
      // top of it. Built once, the first time the map is available.
      map_build_nav(&c.prediction_map);
      nav_ready = true;
    }
    if (c.prediction_ready && c.player_index >= 0 && c.player_index < MAX_PLAYERS) {
      client_view_state(c, view.get());
      in = agent_think(*view, c.prediction_map, c.player_index, AGENT_DEMON, cfg, mem, rng, dt);
    } else {
      in.buttons = BTN_FORWARD;
      in.yaw = std::sin(static_cast<float>(now) * 0.7f) * PI;
    }
    in.sequence = ++sequence;
    client_send_input(c, in);

    std::this_thread::sleep_for(std::chrono::milliseconds(16));
  }
  client_disconnect(c);
  return 0;
}

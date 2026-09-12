#include "client/bot.h"

#include "ai/agent.h"
#include "client/client.h"
#include "core/log.h"
#include "core/rng.h"
#include "game/game_state.h"
#include "game/tuning.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>

static double bot_now() {
  using Clock = std::chrono::steady_clock;
  static Clock::time_point start = Clock::now();
  return std::chrono::duration<double>(Clock::now() - start).count();
}

static int bot_run(NetAddress server, const char* name, int lifetime_seconds, int skill,
                   std::atomic<bool>* stop) {
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
  double last_connected = start;
  uint32_t sequence = 0;
  bool nav_ready = false;
  while (lifetime_seconds <= 0 || bot_now() - start < lifetime_seconds) {
    double now = bot_now();
    float dt = static_cast<float>(now - last);
    last = now;
    if (dt > 0.25f) dt = 0.25f;  // a stall must not teleport the bot's timers

    ClientEvents events{};
    client_receive(c, now, &events);
    // A bot whose server has gone must not sit there forever. Without this a
    // backgrounded bot survives the game that spawned it and turns up in the
    // next session.
    if (c.state == CLIENT_CONNECTED) {
      last_connected = now;
    } else if (c.state == CLIENT_REJECTED || now - last_connected > 10.0) {
      log_info("bot '%s' leaving: no server", name ? name : "bot");
      break;
    }
    if (stop && stop->load(std::memory_order_relaxed)) break;

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

int bot_main(NetAddress server, const char* name, int lifetime_seconds, int skill) {
  return bot_run(server, name, lifetime_seconds, skill, nullptr);
}

struct BotSwarm {
  std::atomic<bool> stop{false};
  std::vector<std::thread> threads;
};

BotSwarm* bot_swarm_start(NetAddress server, int count, int skill) {
  if (count <= 0) return nullptr;
  BotSwarm* swarm = new BotSwarm();
  for (int i = 0; i < count; ++i) {
    char name[16];
    std::snprintf(name, sizeof(name), "bot%d", i + 1);
    std::string owned(name);
    swarm->threads.emplace_back([swarm, server, owned, skill]() {
      bot_run(server, owned.c_str(), 0, skill, &swarm->stop);
    });
  }
  log_info("%d bot%s joining at %s difficulty", count, count == 1 ? "" : "s",
           agent_skill_name(static_cast<AgentSkill>(skill)));
  return swarm;
}

void bot_swarm_stop(BotSwarm* swarm) {
  if (!swarm) return;
  swarm->stop.store(true, std::memory_order_relaxed);
  for (std::thread& t : swarm->threads) {
    if (t.joinable()) t.join();
  }
  delete swarm;
}

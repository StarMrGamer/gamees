#include "client/bot.h"

#include "client/client.h"
#include "game/game_state.h"
#include "game/tuning.h"

#include <chrono>
#include <cmath>
#include <thread>

static double bot_now() {
  using Clock = std::chrono::steady_clock;
  static Clock::time_point start = Clock::now();
  return std::chrono::duration<double>(Clock::now() - start).count();
}

int bot_main(NetAddress server, const char* name, int lifetime_seconds) {
  Client c{};
  if (!client_start(c, server, name ? name : "bot")) return 1;
  double start = bot_now();
  while (lifetime_seconds <= 0 || bot_now() - start < lifetime_seconds) {
    double now = bot_now();
    ClientEvents events{};
    client_receive(c, now, &events);
    PlayerInput in{};
    in.buttons = BTN_FORWARD | BTN_FIRE;
    if (static_cast<int>(now) % 4 == 0) in.buttons |= BTN_JUMP;
    if (static_cast<int>(now) % 7 == 0) in.buttons |= BTN_DASH;
    in.weapon_switch = static_cast<int>(now) % 5 == 0 ? 2 : 1;
    in.yaw = std::sin(static_cast<float>(now) * 0.7f) * PI;
    in.pitch = 0.0f;
    client_send_input(c, in);
    std::this_thread::sleep_for(std::chrono::milliseconds(16));
  }
  client_disconnect(c);
  return 0;
}

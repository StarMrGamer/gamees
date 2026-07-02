#include "server/dedicated.h"

#include "core/log.h"
#include "game/tuning.h"
#include "server/server.h"

#include <atomic>
#include <chrono>
#include <csignal>

static std::atomic<bool> g_stop{false};

static double seconds_now() {
  using Clock = std::chrono::steady_clock;
  static Clock::time_point start = Clock::now();
  return std::chrono::duration<double>(Clock::now() - start).count();
}

static void signal_stop(int) {
  g_stop = true;
}

static void run_server_loop(Server& sv, std::atomic<bool>* stop_flag) {
  double next_tick = seconds_now();
  while (!g_stop && (!stop_flag || !stop_flag->load())) {
    double now = seconds_now();
    server_pump(sv, now);
    while (now >= next_tick) {
      server_tick(sv);
      server_broadcast(sv);
      next_tick += TICK_DT;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

int dedicated_main(uint16_t port, const char* map_path, int frag_limit) {
  g_stop = false;
  std::signal(SIGINT, signal_stop);
  std::signal(SIGTERM, signal_stop);
  Server sv{};
  if (!server_init(sv, port, map_path, frag_limit)) return 1;
  log_info("dedicated server listening on port %u", port);
  run_server_loop(sv, nullptr);
  server_shutdown(sv);
  return 0;
}

bool server_thread_start(ServerThread& st, uint16_t port, const char* map_path, int frag_limit) {
  g_stop = false;
  st.stop = false;
  st.startup_state = 0;
  st.port = port;
  st.map_path = map_path;
  st.frag_limit = frag_limit;
  st.thread = std::thread([&st]() {
    Server sv{};
    if (!server_init(sv, st.port, st.map_path, st.frag_limit)) {
      st.startup_state = -1;
      return;
    }
    st.startup_state = 1;
    run_server_loop(sv, &st.stop);
    server_shutdown(sv);
  });
  while (st.startup_state.load() == 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  if (st.startup_state.load() < 0) {
    if (st.thread.joinable()) st.thread.join();
    return false;
  }
  return true;
}

void server_thread_stop(ServerThread& st) {
  st.stop = true;
  if (st.thread.joinable()) st.thread.join();
}

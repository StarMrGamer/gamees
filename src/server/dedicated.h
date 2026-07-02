#pragma once

#include <atomic>
#include <cstdint>
#include <thread>

int dedicated_main(uint16_t port, const char* map_path, int frag_limit);

struct ServerThread {
  std::thread thread;
  std::atomic<bool> stop;
  std::atomic<int> startup_state;
  uint16_t port;
  const char* map_path;
  int frag_limit;
};

bool server_thread_start(ServerThread& st, uint16_t port, const char* map_path, int frag_limit);
void server_thread_stop(ServerThread& st);

#include <SDL3/SDL_main.h>

#include "client/bot.h"
#include "client/config.h"
#include "client/game_client.h"
#include "client/menu.h"
#include "core/log.h"
#include "game/tuning.h"
#include "net/protocol.h"
#include "platform/socket.h"
#include "server/dedicated.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

enum RunMode {
  MODE_MENU,
  MODE_CONNECT,
  MODE_HOST,
  MODE_DEDICATED,
  MODE_BOT,
};

static bool parse_port(const char* s, uint16_t* out) {
  int p = std::atoi(s);
  if (p <= 0 || p > 65535) return false;
  *out = static_cast<uint16_t>(p);
  return true;
}

static bool parse_sensitivity(const char* s, float* out) {
  if (!s || !out) return false;
  char* end = nullptr;
  float value = std::strtof(s, &end);
  if (end == s || *end != 0 || value < 0.1f || value > 20.0f) return false;
  *out = value;
  return true;
}

static bool parse_player_class_arg(const char* s, uint8_t* out) {
  if (!s || !out) return false;
  if (std::strcmp(s, "ranger") == 0 || std::strcmp(s, "RANGER") == 0) {
    *out = CLASS_RANGER;
    return true;
  }
  if (std::strcmp(s, "scout") == 0 || std::strcmp(s, "SCOUT") == 0) {
    *out = CLASS_SCOUT;
    return true;
  }
  if (std::strcmp(s, "tank") == 0 || std::strcmp(s, "TANK") == 0) {
    *out = CLASS_TANK;
    return true;
  }
  return false;
}

static bool parse_jump_bind_arg(const char* s, uint8_t* out) {
  return client_jump_bind_parse(s, out);
}

static void log_host_join_addresses(uint16_t port) {
  log_info("hosting on UDP port %u", port);
  NetAddress addrs[8]{};
  int count = net_local_addresses(addrs, 8, port);
  if (count <= 0) {
    log_info("friends can join with --connect <your-ip>:%u; could not auto-detect a LAN IPv4 address", port);
    return;
  }
  log_info("friends on your LAN can join with:");
  for (int i = 0; i < count; ++i) {
    char text[64]{};
    net_address_to_string(addrs[i], text, sizeof(text));
    log_info("  ./arena --connect %s --name player2", text);
  }
}

int main(int argc, char** argv) {
  RunMode mode = MODE_MENU;
  const char* address = "127.0.0.1";
  const char* name = "player";
  const char* map_path = "maps/arena.txt";
  uint16_t port = DEFAULT_PORT;
  int fraglimit = DEFAULT_FRAG_LIMIT;
  ClientSettings client_settings{};
  client_config_load(client_settings);

  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--connect") == 0 && i + 1 < argc) {
      mode = MODE_CONNECT;
      address = argv[++i];
    } else if (std::strcmp(argv[i], "--host") == 0) {
      mode = MODE_HOST;
    } else if (std::strcmp(argv[i], "--dedicated") == 0) {
      mode = MODE_DEDICATED;
    } else if (std::strcmp(argv[i], "--bot") == 0 && i + 1 < argc) {
      mode = MODE_BOT;
      address = argv[++i];
    } else if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
      if (!parse_port(argv[++i], &port)) fatal_error("invalid --port");
    } else if (std::strcmp(argv[i], "--fraglimit") == 0 && i + 1 < argc) {
      fraglimit = std::atoi(argv[++i]);
      if (fraglimit <= 0) fatal_error("invalid --fraglimit");
    } else if (std::strcmp(argv[i], "--name") == 0 && i + 1 < argc) {
      name = argv[++i];
    } else if (std::strcmp(argv[i], "--map") == 0 && i + 1 < argc) {
      map_path = argv[++i];
    } else if (std::strcmp(argv[i], "--sensitivity") == 0 && i + 1 < argc) {
      if (!parse_sensitivity(argv[++i], &client_settings.sensitivity)) {
        fatal_error("invalid --sensitivity, use a value from 0.1 to 20.0");
      }
    } else if (std::strcmp(argv[i], "--class") == 0 && i + 1 < argc) {
      if (!parse_player_class_arg(argv[++i], &client_settings.player_class)) {
        fatal_error("invalid --class, use ranger, scout, or tank");
      }
    } else if (std::strcmp(argv[i], "--jump") == 0 && i + 1 < argc) {
      if (!parse_jump_bind_arg(argv[++i], &client_settings.jump_bind)) {
        fatal_error("invalid --jump, use space, mwheelup, or mwheeldown");
      }
    } else if (std::strcmp(argv[i], "--fly") == 0) {
      // Accepted for the world milestone; the current client always uses player camera.
    } else {
      fatal_error("unknown or incomplete argument '%s'", argv[i]);
    }
  }

  char menu_addr[64]{};
  if (mode == MODE_MENU) {
    MenuResult r = menu_run(menu_addr, sizeof(menu_addr));
    if (r == MENU_QUIT) return 0;
    if (r == MENU_HOST) mode = MODE_HOST;
    if (r == MENU_JOIN) {
      mode = MODE_CONNECT;
      address = menu_addr;
    }
  }

  if (mode == MODE_DEDICATED) {
    return dedicated_main(port, map_path, fraglimit);
  }

  if (mode == MODE_HOST) {
    ServerThread st{};
    if (!server_thread_start(st, port, map_path, fraglimit)) return 1;
    log_host_join_addresses(port);
    char local[64];
    std::snprintf(local, sizeof(local), "127.0.0.1:%u", port);
    NetAddress server{};
    if (!net_address_parse(local, port, &server)) fatal_error("failed to parse loopback address");
    return game_client_main(server, name, &st, map_path, client_settings);
  }

  NetAddress server{};
  if (!net_address_parse(address, port, &server)) {
    fatal_error("invalid address '%s'", address);
  }

  if (mode == MODE_BOT) {
    return bot_main(server, name, 0);
  }

  return game_client_main(server, name, nullptr, map_path, client_settings);
}

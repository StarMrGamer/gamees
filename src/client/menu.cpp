#include "client/menu.h"

#include "net/protocol.h"

#include <cstdio>
#include <cstring>

MenuResult menu_run(char* out_address, int cap) {
  std::printf("arena\n");
  std::printf("1. Host\n");
  std::printf("2. Join by IP\n");
  std::printf("3. Quit\n");
  std::printf("> ");
  char line[64]{};
  if (!std::fgets(line, sizeof(line), stdin)) return MENU_QUIT;
  if (line[0] == '1') return MENU_HOST;
  if (line[0] == '2') {
    std::printf("Server IP[:port] (blank for localhost): ");
    char addr[64]{};
    if (!std::fgets(addr, sizeof(addr), stdin)) return MENU_QUIT;
    size_t len = std::strlen(addr);
    while (len > 0 && (addr[len - 1] == '\n' || addr[len - 1] == '\r')) {
      addr[--len] = 0;
    }
    if (out_address && cap > 0) {
      if (addr[0]) {
        std::snprintf(out_address, static_cast<size_t>(cap), "%s", addr);
      } else {
        std::snprintf(out_address, static_cast<size_t>(cap), "127.0.0.1:%u", DEFAULT_PORT);
      }
    }
    return MENU_JOIN;
  }
  return MENU_QUIT;
}

#include "platform/socket.h"

#include "core/log.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
using SocketHandle = SOCKET;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using SocketHandle = int;
constexpr int INVALID_SOCKET = -1;
constexpr int SOCKET_ERROR = -1;
#endif

bool net_init() {
#ifdef _WIN32
  WSADATA data;
  return WSAStartup(MAKEWORD(2, 2), &data) == 0;
#else
  return true;
#endif
}

void net_shutdown() {
#ifdef _WIN32
  WSACleanup();
#endif
}

static SocketHandle socket_from(const UdpSocket& s) {
  return static_cast<SocketHandle>(s.handle);
}

bool net_address_parse(const char* str, uint16_t default_port, NetAddress* out) {
  if (!str || !out) return false;
  char host[128]{};
  uint16_t port = default_port;
  const char* colon = std::strrchr(str, ':');
  if (colon) {
    size_t n = static_cast<size_t>(colon - str);
    if (n >= sizeof(host)) return false;
    std::memcpy(host, str, n);
    int p = std::atoi(colon + 1);
    if (p <= 0 || p > 65535) return false;
    port = static_cast<uint16_t>(p);
  } else {
    std::snprintf(host, sizeof(host), "%s", str);
  }

  uint32_t ip = 0;
  if (std::strcmp(host, "localhost") == 0) {
    ip = (127u << 24) | 1u;
  } else {
    in_addr addr{};
    if (inet_pton(AF_INET, host, &addr) != 1) return false;
    ip = ntohl(addr.s_addr);
  }
  out->ip = ip;
  out->port = port;
  return true;
}

void net_address_to_string(NetAddress a, char* buf, int cap) {
  if (!buf || cap <= 0) return;
  in_addr addr{};
  addr.s_addr = htonl(a.ip);
  char ip[64]{};
  const char* s = inet_ntop(AF_INET, &addr, ip, sizeof(ip));
  std::snprintf(buf, static_cast<size_t>(cap), "%s:%u", s ? s : "0.0.0.0", a.port);
}

bool net_address_equal(NetAddress a, NetAddress b) {
  return a.ip == b.ip && a.port == b.port;
}

int net_local_addresses(NetAddress* out, int cap, uint16_t port) {
  if (!out || cap <= 0) return 0;
  if (!net_init()) return 0;

  char host[256]{};
  if (gethostname(host, sizeof(host) - 1) != 0) {
    net_shutdown();
    return 0;
  }

  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  addrinfo* result = nullptr;
  if (getaddrinfo(host, nullptr, &hints, &result) != 0) {
    net_shutdown();
    return 0;
  }

  int count = 0;
  for (addrinfo* it = result; it && count < cap; it = it->ai_next) {
    if (!it->ai_addr || static_cast<size_t>(it->ai_addrlen) < sizeof(sockaddr_in)) continue;
    const sockaddr_in* addr = reinterpret_cast<const sockaddr_in*>(it->ai_addr);
    uint32_t ip = ntohl(addr->sin_addr.s_addr);
    if (ip == 0 || (ip >> 24) == 127) continue;

    bool duplicate = false;
    for (int i = 0; i < count; ++i) {
      if (out[i].ip == ip) {
        duplicate = true;
        break;
      }
    }
    if (duplicate) continue;

    out[count++] = {ip, port};
  }
  freeaddrinfo(result);
  net_shutdown();
  return count;
}

UdpSocket udp_open(uint16_t bind_port) {
  UdpSocket out{};
  SocketHandle fd = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (fd == INVALID_SOCKET) {
    log_error("udp socket creation failed");
    return out;
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(bind_port);
  if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
    log_error("udp bind failed on port %u", bind_port);
#ifdef _WIN32
    closesocket(fd);
#else
    close(fd);
#endif
    return out;
  }

#ifdef _WIN32
  u_long nonblock = 1;
  ioctlsocket(fd, FIONBIO, &nonblock);
#else
  int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif

  out.handle = static_cast<uint64_t>(fd);
  out.valid = true;
  return out;
}

void udp_close(UdpSocket& s) {
  if (!s.valid) return;
  SocketHandle fd = socket_from(s);
#ifdef _WIN32
  closesocket(fd);
#else
  close(fd);
#endif
  s.valid = false;
  s.handle = 0;
}

bool udp_send(UdpSocket& s, NetAddress to, const void* data, int len) {
  if (!s.valid || !data || len <= 0) return false;
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(to.ip);
  addr.sin_port = htons(to.port);
  int sent = sendto(socket_from(s), static_cast<const char*>(data), len, 0,
                    reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
  return sent == len;
}

int udp_recv(UdpSocket& s, void* buf, int cap, NetAddress* from) {
  if (!s.valid || !buf || cap <= 0) return -1;
  sockaddr_in addr{};
#ifdef _WIN32
  int addr_len = sizeof(addr);
#else
  socklen_t addr_len = sizeof(addr);
#endif
  int got = recvfrom(socket_from(s), static_cast<char*>(buf), cap, 0,
                     reinterpret_cast<sockaddr*>(&addr), &addr_len);
  if (got <= 0) return -1;
  if (from) {
    from->ip = ntohl(addr.sin_addr.s_addr);
    from->port = ntohs(addr.sin_port);
  }
  return got;
}

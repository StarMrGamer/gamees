#pragma once

#include <cstdint>

bool net_init();
void net_shutdown();

struct NetAddress {
  uint32_t ip;
  uint16_t port;
};

bool net_address_parse(const char* str, uint16_t default_port, NetAddress* out);
void net_address_to_string(NetAddress a, char* buf, int cap);
bool net_address_equal(NetAddress a, NetAddress b);

struct UdpSocket {
  uint64_t handle;
  bool valid;
};

UdpSocket udp_open(uint16_t bind_port);
void udp_close(UdpSocket& s);
bool udp_send(UdpSocket& s, NetAddress to, const void* data, int len);
int udp_recv(UdpSocket& s, void* buf, int cap, NetAddress* from);

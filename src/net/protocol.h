#pragma once

#include "core/math.h"

#include <cstdint>

constexpr uint32_t PROTOCOL_MAGIC = 0x414E5241u;
// 7: added the sniper class and weapon. The wire format is unchanged, but a
// v6 peer would read class 4 as out of range and silently fall back to Ranger.
constexpr uint8_t PROTOCOL_VERSION = 7;
constexpr int MAX_PACKET = 4096;
constexpr uint16_t DEFAULT_PORT = 27950;

enum PacketType : uint8_t {
  PKT_CL_HELLO = 1,
  PKT_CL_INPUT,
  PKT_CL_DISCONNECT,
  PKT_SV_ACCEPT,
  PKT_SV_REJECT,
  PKT_SV_SNAPSHOT,
  PKT_SV_SHUTDOWN,
};

struct NetWriter {
  uint8_t* buf;
  int cap;
  int len;
  bool overflow;
};

void nw_init(NetWriter& w, uint8_t* buf, int cap);
void nw_u8(NetWriter& w, uint8_t v);
void nw_u16(NetWriter& w, uint16_t v);
void nw_u32(NetWriter& w, uint32_t v);
void nw_f32(NetWriter& w, float v);
void nw_vec3(NetWriter& w, Vec3 v);
void nw_string(NetWriter& w, const char* s, int max_len);

struct NetReader {
  const uint8_t* buf;
  int len;
  int pos;
  bool error;
};

void nr_init(NetReader& r, const uint8_t* buf, int len);
uint8_t nr_u8(NetReader& r);
uint16_t nr_u16(NetReader& r);
uint32_t nr_u32(NetReader& r);
float nr_f32(NetReader& r);
Vec3 nr_vec3(NetReader& r);
void nr_string(NetReader& r, char* out, int cap);

void packet_header_write(NetWriter& w, PacketType type);
bool packet_header_read(NetReader& r, PacketType* type_out);

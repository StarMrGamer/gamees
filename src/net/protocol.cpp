#include "net/protocol.h"

#include <cstring>

static bool nw_space(NetWriter& w, int bytes) {
  if (w.overflow) return false;
  if (bytes < 0 || w.len + bytes > w.cap) {
    w.overflow = true;
    return false;
  }
  return true;
}

void nw_init(NetWriter& w, uint8_t* buf, int cap) {
  w.buf = buf;
  w.cap = cap;
  w.len = 0;
  w.overflow = false;
}

void nw_u8(NetWriter& w, uint8_t v) {
  if (!nw_space(w, 1)) return;
  w.buf[w.len++] = v;
}

void nw_u16(NetWriter& w, uint16_t v) {
  if (!nw_space(w, 2)) return;
  w.buf[w.len++] = static_cast<uint8_t>(v & 0xff);
  w.buf[w.len++] = static_cast<uint8_t>((v >> 8) & 0xff);
}

void nw_u32(NetWriter& w, uint32_t v) {
  if (!nw_space(w, 4)) return;
  for (int i = 0; i < 4; ++i) w.buf[w.len++] = static_cast<uint8_t>((v >> (i * 8)) & 0xff);
}

void nw_f32(NetWriter& w, float v) {
  uint32_t bits = 0;
  std::memcpy(&bits, &v, sizeof(bits));
  nw_u32(w, bits);
}

void nw_vec3(NetWriter& w, Vec3 v) {
  nw_f32(w, v.x);
  nw_f32(w, v.y);
  nw_f32(w, v.z);
}

void nw_string(NetWriter& w, const char* s, int max_len) {
  int len = 0;
  while (s && s[len] && len < max_len && len < 255) ++len;
  nw_u8(w, static_cast<uint8_t>(len));
  if (!nw_space(w, len)) return;
  if (len > 0) std::memcpy(w.buf + w.len, s, static_cast<size_t>(len));
  w.len += len;
}

void nr_init(NetReader& r, const uint8_t* buf, int len) {
  r.buf = buf;
  r.len = len;
  r.pos = 0;
  r.error = false;
}

static bool nr_space(NetReader& r, int bytes) {
  if (r.error) return false;
  if (bytes < 0 || r.pos + bytes > r.len) {
    r.error = true;
    return false;
  }
  return true;
}

uint8_t nr_u8(NetReader& r) {
  if (!nr_space(r, 1)) return 0;
  return r.buf[r.pos++];
}

uint16_t nr_u16(NetReader& r) {
  if (!nr_space(r, 2)) return 0;
  uint16_t v = static_cast<uint16_t>(r.buf[r.pos]) |
               (static_cast<uint16_t>(r.buf[r.pos + 1]) << 8);
  r.pos += 2;
  return v;
}

uint32_t nr_u32(NetReader& r) {
  if (!nr_space(r, 4)) return 0;
  uint32_t v = 0;
  for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(r.buf[r.pos + i]) << (i * 8);
  r.pos += 4;
  return v;
}

float nr_f32(NetReader& r) {
  uint32_t bits = nr_u32(r);
  float v = 0.0f;
  std::memcpy(&v, &bits, sizeof(v));
  return v;
}

Vec3 nr_vec3(NetReader& r) {
  return {nr_f32(r), nr_f32(r), nr_f32(r)};
}

void nr_string(NetReader& r, char* out, int cap) {
  uint8_t len = nr_u8(r);
  if (cap > 0) out[0] = 0;
  if (!nr_space(r, len)) return;
  int copy = cap > 0 ? static_cast<int>(len) : 0;
  if (copy > cap - 1) copy = cap - 1;
  if (copy > 0) std::memcpy(out, r.buf + r.pos, static_cast<size_t>(copy));
  if (cap > 0) out[copy] = 0;
  r.pos += len;
}

void packet_header_write(NetWriter& w, PacketType type) {
  nw_u32(w, PROTOCOL_MAGIC);
  nw_u8(w, PROTOCOL_VERSION);
  nw_u8(w, static_cast<uint8_t>(type));
}

bool packet_header_read(NetReader& r, PacketType* type_out) {
  uint32_t magic = nr_u32(r);
  uint8_t version = nr_u8(r);
  uint8_t type = nr_u8(r);
  if (r.error || magic != PROTOCOL_MAGIC || version != PROTOCOL_VERSION) return false;
  if (type_out) *type_out = static_cast<PacketType>(type);
  return true;
}

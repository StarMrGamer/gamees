#include "test_harness.h"

#include "net/protocol.h"

#include <cstring>

TEST(packet_roundtrip) {
  uint8_t buf[128];
  NetWriter w;
  nw_init(w, buf, sizeof(buf));
  packet_header_write(w, PKT_CL_HELLO);
  nw_u16(w, 0xBEEF);
  nw_f32(w, 3.5f);
  nw_vec3(w, {1, 2, 3});
  nw_string(w, "abcdefghijklmnop", 15);
  CHECK(!w.overflow);

  NetReader r;
  nr_init(r, buf, w.len);
  PacketType type{};
  CHECK(packet_header_read(r, &type));
  CHECK_EQ_INT(type, PKT_CL_HELLO);
  CHECK_EQ_INT(nr_u16(r), 0xBEEF);
  CHECK_NEAR(nr_f32(r), 3.5f, 0.0001f);
  Vec3 v = nr_vec3(r);
  CHECK_NEAR(v.z, 3.0f, 0.0001f);
  char s[16];
  nr_string(r, s, sizeof(s));
  CHECK(std::strcmp(s, "abcdefghijklmno") == 0);
  CHECK(!r.error);
}

TEST(packet_truncation_sets_error) {
  uint8_t buf[8];
  NetWriter w;
  nw_init(w, buf, sizeof(buf));
  nw_u32(w, 1);
  nw_u32(w, 2);
  NetReader r;
  nr_init(r, buf, 7);
  (void)nr_u32(r);
  (void)nr_u32(r);
  CHECK(r.error);
}

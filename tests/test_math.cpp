#include "test_harness.h"

#include "core/math.h"

TEST(vec_ops_and_angles) {
  Vec3 a{1, 2, 3};
  Vec3 b{4, 5, 6};
  CHECK_NEAR(vec3_dot(a, b), 32.0f, 0.0001f);
  Vec3 c = vec3_cross({1, 0, 0}, {0, 1, 0});
  CHECK_NEAR(c.z, 1.0f, 0.0001f);
  Vec3 f = angles_forward(0.0f, 0.0f);
  CHECK_NEAR(f.x, 0.0f, 0.0001f);
  CHECK_NEAR(f.z, -1.0f, 0.0001f);
  Vec3 r = angles_right(0.0f);
  CHECK_NEAR(r.x, 1.0f, 0.0001f);
}

TEST(mat4_translation) {
  Mat4 t = mat4_translate({2, 3, 4});
  CHECK_NEAR(t.m[12], 2.0f, 0.0001f);
  CHECK_NEAR(t.m[13], 3.0f, 0.0001f);
  CHECK_NEAR(t.m[14], 4.0f, 0.0001f);
}

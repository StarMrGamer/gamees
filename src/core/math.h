#pragma once

#include <cmath>
#include <cstdint>

constexpr float PI = 3.14159265358979323846f;

struct Vec2 {
  float x, y;
};

struct Vec3 {
  float x, y, z;
};

struct Mat4 {
  float m[16];
};

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator-(Vec3 v) { return {-v.x, -v.y, -v.z}; }
inline Vec3 operator*(Vec3 v, float s) { return {v.x * s, v.y * s, v.z * s}; }
inline Vec3 operator*(float s, Vec3 v) { return v * s; }
inline Vec3 operator*(Vec3 a, Vec3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
inline Vec3 operator/(Vec3 v, float s) { return {v.x / s, v.y / s, v.z / s}; }
inline Vec3& operator+=(Vec3& a, Vec3 b) { a = a + b; return a; }
inline Vec3& operator-=(Vec3& a, Vec3 b) { a = a - b; return a; }
inline Vec3& operator*=(Vec3& a, float s) { a = a * s; return a; }

inline float clampf(float v, float lo, float hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

inline float lerp(float a, float b, float t) {
  return a + (b - a) * t;
}

inline float vec3_dot(Vec3 a, Vec3 b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3 vec3_cross(Vec3 a, Vec3 b) {
  return {
    a.y * b.z - a.z * b.y,
    a.z * b.x - a.x * b.z,
    a.x * b.y - a.y * b.x,
  };
}

inline float vec3_length(Vec3 v) {
  return std::sqrt(vec3_dot(v, v));
}

inline Vec3 vec3_normalize(Vec3 v) {
  float len = vec3_length(v);
  if (len <= 0.000001f) return {0, 0, 0};
  return v / len;
}

inline Vec3 vec3_lerp(Vec3 a, Vec3 b, float t) {
  return {lerp(a.x, b.x, t), lerp(a.y, b.y, t), lerp(a.z, b.z, t)};
}

inline float angle_wrap(float a) {
  while (a > PI) a -= PI * 2.0f;
  while (a < -PI) a += PI * 2.0f;
  return a;
}

inline float angle_lerp(float a, float b, float t) {
  return a + angle_wrap(b - a) * t;
}

inline Vec3 angles_forward(float yaw, float pitch) {
  float cp = std::cos(pitch);
  return {std::sin(yaw) * cp, std::sin(pitch), -std::cos(yaw) * cp};
}

inline Vec3 angles_right(float yaw) {
  return {std::cos(yaw), 0.0f, std::sin(yaw)};
}

inline Mat4 mat4_identity() {
  Mat4 r{};
  r.m[0] = 1.0f;
  r.m[5] = 1.0f;
  r.m[10] = 1.0f;
  r.m[15] = 1.0f;
  return r;
}

inline Mat4 mat4_mul(const Mat4& a, const Mat4& b) {
  Mat4 r{};
  for (int c = 0; c < 4; ++c) {
    for (int row = 0; row < 4; ++row) {
      r.m[c * 4 + row] =
        a.m[0 * 4 + row] * b.m[c * 4 + 0] +
        a.m[1 * 4 + row] * b.m[c * 4 + 1] +
        a.m[2 * 4 + row] * b.m[c * 4 + 2] +
        a.m[3 * 4 + row] * b.m[c * 4 + 3];
    }
  }
  return r;
}

inline Mat4 mat4_perspective(float fovy_rad, float aspect, float znear, float zfar) {
  float f = 1.0f / std::tan(fovy_rad * 0.5f);
  Mat4 r{};
  r.m[0] = f / aspect;
  r.m[5] = f;
  r.m[10] = (zfar + znear) / (znear - zfar);
  r.m[11] = -1.0f;
  r.m[14] = (2.0f * zfar * znear) / (znear - zfar);
  return r;
}

inline Mat4 mat4_ortho(float l, float rgt, float b, float t, float zn, float zf) {
  Mat4 r = mat4_identity();
  r.m[0] = 2.0f / (rgt - l);
  r.m[5] = 2.0f / (t - b);
  r.m[10] = -2.0f / (zf - zn);
  r.m[12] = -(rgt + l) / (rgt - l);
  r.m[13] = -(t + b) / (t - b);
  r.m[14] = -(zf + zn) / (zf - zn);
  return r;
}

inline Mat4 mat4_look_at(Vec3 eye, Vec3 center, Vec3 up) {
  Vec3 f = vec3_normalize(center - eye);
  Vec3 s = vec3_normalize(vec3_cross(f, up));
  Vec3 u = vec3_cross(s, f);

  Mat4 r = mat4_identity();
  r.m[0] = s.x; r.m[4] = s.y; r.m[8] = s.z;
  r.m[1] = u.x; r.m[5] = u.y; r.m[9] = u.z;
  r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
  r.m[12] = -vec3_dot(s, eye);
  r.m[13] = -vec3_dot(u, eye);
  r.m[14] = vec3_dot(f, eye);
  return r;
}

inline Mat4 mat4_translate(Vec3 t) {
  Mat4 r = mat4_identity();
  r.m[12] = t.x;
  r.m[13] = t.y;
  r.m[14] = t.z;
  return r;
}

inline Mat4 mat4_scale(Vec3 s) {
  Mat4 r = mat4_identity();
  r.m[0] = s.x;
  r.m[5] = s.y;
  r.m[10] = s.z;
  return r;
}

inline Mat4 mat4_rotate_x(float rad) {
  Mat4 r = mat4_identity();
  float c = std::cos(rad);
  float s = std::sin(rad);
  r.m[5] = c; r.m[9] = -s;
  r.m[6] = s; r.m[10] = c;
  return r;
}

inline Mat4 mat4_rotate_y(float rad) {
  Mat4 r = mat4_identity();
  float c = std::cos(rad);
  float s = std::sin(rad);
  r.m[0] = c; r.m[8] = s;
  r.m[2] = -s; r.m[10] = c;
  return r;
}

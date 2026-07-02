#pragma once

#include <cstdint>

struct Rng {
  uint64_t state;
};

inline uint64_t rng_next(Rng& r) {
  if (r.state == 0) r.state = 0x9e3779b97f4a7c15ull;
  uint64_t x = r.state;
  x ^= x >> 12;
  x ^= x << 25;
  x ^= x >> 27;
  r.state = x;
  return x * 2685821657736338717ull;
}

inline float rng_float(Rng& r, float lo, float hi) {
  uint32_t bits = static_cast<uint32_t>(rng_next(r) >> 40);
  float t = static_cast<float>(bits) / static_cast<float>(0xFFFFFFu);
  return lo + (hi - lo) * t;
}

inline int rng_int(Rng& r, int lo, int hi) {
  uint64_t span = static_cast<uint64_t>(hi - lo + 1);
  return lo + static_cast<int>(rng_next(r) % span);
}

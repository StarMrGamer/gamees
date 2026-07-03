#include "audio/synth.h"

#include "game/game_state.h"

#include <cmath>
#include <cstring>

static float noise(uint32_t& state) {
  state = state * 1664525u + 1013904223u;
  uint32_t bits = (state >> 9) | 0x3f800000u;
  float v;
  std::memcpy(&v, &bits, sizeof(v));
  return (v - 1.5f) * 2.0f;
}

static float sine(float freq, float t) {
  return std::sin(t * freq * 2.0f * PI);
}

static float square(float freq, float t) {
  return sine(freq, t) >= 0.0f ? 1.0f : -1.0f;
}

static float saw(float freq, float t) {
  float p = std::fmod(freq * t, 1.0f);
  return p * 2.0f - 1.0f;
}

static float env_decay(float u) {
  return (1.0f - u) * (1.0f - u);
}

static float env_attack_decay(float u, float attack) {
  float a = attack > 0.0f ? clampf(u / attack, 0.0f, 1.0f) : 1.0f;
  return a * env_decay(u);
}

template <typename Fn>
static Sound render(float seconds, float gain, Fn fn) {
  Sound s;
  int count = static_cast<int>(AUDIO_SAMPLE_RATE * seconds);
  s.samples.resize(count);
  uint32_t rng = 0x31415926u;
  for (int i = 0; i < count; ++i) {
    float t = static_cast<float>(i) / AUDIO_SAMPLE_RATE;
    float u = static_cast<float>(i) / static_cast<float>(count > 1 ? count - 1 : 1);
    s.samples[i] = clampf(fn(t, u, rng) * gain, -1.0f, 1.0f);
  }
  return s;
}

Sound synth_make(int sound_id) {
  switch (sound_id) {
    case SND_RIFLE:
      return render(0.09f, 0.92f, [](float t, float u, uint32_t& rng) {
        float crack = square(140.0f + 900.0f * (1.0f - u), t) * env_decay(u);
        float snap = noise(rng) * std::pow(1.0f - u, 6.0f);
        return crack * 0.48f + snap * 0.52f;
      });
    case SND_ROCKET_LAUNCH:
      return render(0.22f, 0.78f, [](float t, float u, uint32_t& rng) {
        float motor = saw(70.0f + 35.0f * u, t) * env_attack_decay(u, 0.08f);
        float rush = noise(rng) * (1.0f - u) * 0.35f;
        return motor * 0.62f + rush;
      });
    case SND_EXPLOSION:
      return render(0.62f, 1.0f, [](float t, float u, uint32_t& rng) {
        float boom = sine(42.0f - 18.0f * u, t) * std::pow(1.0f - u, 1.6f);
        float blast = noise(rng) * std::pow(1.0f - u, 2.5f);
        return boom * 0.82f + blast * 0.42f;
      });
    case SND_JUMP:
      return render(0.10f, 0.42f, [](float t, float u, uint32_t&) {
        return sine(300.0f + 210.0f * u, t) * env_attack_decay(u, 0.15f);
      });
    case SND_DASH:
      return render(0.16f, 0.55f, [](float t, float u, uint32_t& rng) {
        float whoosh = noise(rng) * std::sin(u * PI) * 0.65f;
        float tone = sine(220.0f + 150.0f * u, t) * env_decay(u) * 0.35f;
        return whoosh + tone;
      });
    case SND_SLIDE:
      return render(0.36f, 0.48f, [](float t, float u, uint32_t& rng) {
        float scrape_gate = 0.55f + 0.45f * square(28.0f, t);
        return noise(rng) * scrape_gate * env_decay(u);
      });
    case SND_PICKUP:
      return render(0.18f, 0.52f, [](float t, float u, uint32_t&) {
        float a = sine(660.0f, t) * env_decay(u);
        float b = sine(u > 0.42f ? 990.0f : 880.0f, t) * std::sin(u * PI);
        return a * 0.55f + b * 0.45f;
      });
    case SND_HURT:
      return render(0.18f, 0.48f, [](float t, float u, uint32_t& rng) {
        return (sine(150.0f - 35.0f * u, t) * 0.65f + noise(rng) * 0.25f) * env_decay(u);
      });
    case SND_DEATH:
      return render(0.42f, 0.74f, [](float t, float u, uint32_t& rng) {
        float fall = sine(170.0f - 112.0f * u, t);
        return (fall * 0.82f + noise(rng) * 0.18f) * env_attack_decay(u, 0.04f);
      });
    case SND_RESPAWN:
      return render(0.30f, 0.54f, [](float t, float u, uint32_t&) {
        float shimmer = sine(430.0f + 360.0f * u, t) + sine(645.0f + 240.0f * u, t) * 0.55f;
        return shimmer * env_attack_decay(u, 0.18f);
      });
    case SND_LAND:
      return render(0.11f, 0.46f, [](float t, float u, uint32_t& rng) {
        float thud = sine(96.0f - 34.0f * u, t) * env_decay(u);
        float scuff = noise(rng) * std::pow(1.0f - u, 5.0f);
        return thud * 0.78f + scuff * 0.30f;
      });
    case SND_SHOTGUN:
      return render(0.24f, 1.0f, [](float t, float u, uint32_t& rng) {
        float boom = sine(90.0f - 48.0f * u, t) * std::pow(1.0f - u, 1.5f);
        float body = square(150.0f + 260.0f * (1.0f - u), t) * env_decay(u) * 0.4f;
        float blast = noise(rng) * std::pow(1.0f - u, 2.2f);
        return boom * 0.7f + body + blast * 0.55f;
      });
    case SND_LMG:
      return render(0.06f, 0.7f, [](float t, float u, uint32_t& rng) {
        float crack = square(200.0f + 620.0f * (1.0f - u), t) * env_decay(u);
        float snap = noise(rng) * std::pow(1.0f - u, 8.0f);
        return crack * 0.4f + snap * 0.6f;
      });
    default:
      return render(0.10f, 0.30f, [](float t, float u, uint32_t&) {
        return sine(220.0f, t) * env_decay(u);
      });
  }
}

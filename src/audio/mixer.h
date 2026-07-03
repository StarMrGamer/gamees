#pragma once

#include "core/math.h"

#include <cstdint>
#include <vector>

constexpr int AUDIO_SAMPLE_RATE = 48000;
constexpr int AUDIO_MAX_VOICES = 64;

struct Sound {
  std::vector<float> samples;
};

struct Voice {
  bool active;
  int sound_id;
  float cursor;
  float rate;
  float left_gain;
  float right_gain;
  uint32_t started;
};

struct SDL_AudioStream;

struct Mixer {
  Sound sounds[32];
  Voice voices[AUDIO_MAX_VOICES];
  SDL_AudioStream* stream;
  Vec3 listener_pos;
  float listener_yaw;
  uint32_t voice_clock;
  uint32_t pitch_rng;
  bool enabled;
  bool owns_audio_subsystem;
};

bool audio_init(Mixer& m);
void audio_shutdown(Mixer& m);
void audio_register(Mixer& m, int sound_id, Sound s);
void audio_set_listener(Mixer& m, Vec3 pos, float yaw);
void audio_play(Mixer& m, int sound_id, float gain);
void audio_play_3d(Mixer& m, int sound_id, Vec3 pos, float gain);
float audio_attenuation(float dist);
void audio_pan(Vec3 listener_pos, float listener_yaw, Vec3 src, float* left_gain, float* right_gain);

#pragma once

#include "core/math.h"

#include <vector>

constexpr int AUDIO_SAMPLE_RATE = 48000;

struct Sound {
  std::vector<float> samples;
};

struct Mixer {
  Sound sounds[32];
  Vec3 listener_pos;
  float listener_yaw;
  bool enabled;
};

bool audio_init(Mixer& m);
void audio_shutdown(Mixer& m);
void audio_register(Mixer& m, int sound_id, Sound s);
void audio_set_listener(Mixer& m, Vec3 pos, float yaw);
void audio_play(Mixer& m, int sound_id, float gain);
void audio_play_3d(Mixer& m, int sound_id, Vec3 pos, float gain);
float audio_attenuation(float dist);
void audio_pan(Vec3 listener_pos, float listener_yaw, Vec3 src, float* left_gain, float* right_gain);

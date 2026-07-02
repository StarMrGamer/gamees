#include "audio/mixer.h"

#include "core/log.h"

#include <cmath>

bool audio_init(Mixer& m) {
  m = {};
  m.enabled = false;
  log_warn("audio mixer running silently in this build");
  return false;
}

void audio_shutdown(Mixer&) {}

void audio_register(Mixer& m, int sound_id, Sound s) {
  if (sound_id >= 0 && sound_id < 32) m.sounds[sound_id] = std::move(s);
}

void audio_set_listener(Mixer& m, Vec3 pos, float yaw) {
  m.listener_pos = pos;
  m.listener_yaw = yaw;
}

void audio_play(Mixer&, int, float) {}
void audio_play_3d(Mixer&, int, Vec3, float) {}

float audio_attenuation(float dist) {
  return 1.0f / (1.0f + 0.08f * dist + 0.01f * dist * dist);
}

void audio_pan(Vec3 listener_pos, float listener_yaw, Vec3 src, float* left_gain, float* right_gain) {
  Vec3 delta = src - listener_pos;
  float dist = vec3_length(delta);
  Vec3 dir = dist > 0.001f ? delta / dist : Vec3{0, 0, -1};
  Vec3 right = angles_right(listener_yaw);
  float pan = clampf(vec3_dot(dir, right), -1.0f, 1.0f);
  float att = audio_attenuation(dist);
  if (left_gain) *left_gain = att * (pan <= 0.0f ? 1.0f : 1.0f - pan);
  if (right_gain) *right_gain = att * (pan >= 0.0f ? 1.0f : 1.0f + pan);
}

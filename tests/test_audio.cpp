#include "test_harness.h"

#include "audio/mixer.h"
#include "audio/synth.h"
#include "game/game_state.h"

#include <cmath>

static float sound_peak(const Sound& s) {
  float peak = 0.0f;
  for (float sample : s.samples) {
    float a = std::fabs(sample);
    if (a > peak) peak = a;
  }
  return peak;
}

static float sound_energy(const Sound& s) {
  float energy = 0.0f;
  for (float sample : s.samples) energy += sample * sample;
  return energy;
}

TEST(synth_generates_bounded_nonempty_game_sounds) {
  for (int id = 1; id < SND_COUNT; ++id) {
    Sound s = synth_make(id);
    CHECK(!s.samples.empty());
    CHECK(sound_peak(s) <= 1.0001f);
    CHECK(sound_energy(s) > 0.001f);
  }
}

TEST(synth_uses_longer_explosion_than_rifle) {
  Sound rifle = synth_make(SND_RIFLE);
  Sound explosion = synth_make(SND_EXPLOSION);
  CHECK(explosion.samples.size() > rifle.samples.size() * 4);
}

TEST(audio_pan_tracks_listener_yaw) {
  float left = 0.0f;
  float right = 0.0f;
  audio_pan({0, 0, 0}, 0.0f, {0, 0, -10}, &left, &right);
  CHECK_NEAR(left, right, 0.0001f);

  audio_pan({0, 0, 0}, 0.0f, {10, 0, 0}, &left, &right);
  CHECK(right > left);

  audio_pan({0, 0, 0}, 0.0f, {-10, 0, 0}, &left, &right);
  CHECK(left > right);
}

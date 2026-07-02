#include "audio/synth.h"

#include "game/game_state.h"

#include <cmath>

static Sound tone(float freq, float seconds, float gain) {
  Sound s;
  int count = static_cast<int>(AUDIO_SAMPLE_RATE * seconds);
  s.samples.resize(count);
  for (int i = 0; i < count; ++i) {
    float t = static_cast<float>(i) / AUDIO_SAMPLE_RATE;
    float env = 1.0f - static_cast<float>(i) / static_cast<float>(count);
    s.samples[i] = std::sin(t * freq * 2.0f * PI) * env * gain;
  }
  return s;
}

Sound synth_make(int sound_id) {
  switch (sound_id) {
    case SND_RIFLE: return tone(180.0f, 0.08f, 0.8f);
    case SND_ROCKET_LAUNCH: return tone(90.0f, 0.18f, 0.7f);
    case SND_EXPLOSION: return tone(55.0f, 0.35f, 1.0f);
    case SND_JUMP: return tone(420.0f, 0.08f, 0.4f);
    case SND_DASH: return tone(260.0f, 0.12f, 0.5f);
    case SND_PICKUP: return tone(720.0f, 0.12f, 0.5f);
    case SND_DEATH: return tone(80.0f, 0.35f, 0.8f);
    case SND_RESPAWN: return tone(520.0f, 0.2f, 0.5f);
    default: return tone(220.0f, 0.1f, 0.3f);
  }
}

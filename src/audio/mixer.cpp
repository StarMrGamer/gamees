#include "audio/mixer.h"

#include "core/log.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

static float clamp_sample(float v) {
  v = v / (1.0f + std::fabs(v) * 0.35f);
  return clampf(v, -1.0f, 1.0f);
}

static void mix_chunk(Mixer& m, float* out, int frames) {
  std::fill(out, out + frames * 2, 0.0f);
  for (int voice_i = 0; voice_i < AUDIO_MAX_VOICES; ++voice_i) {
    Voice& v = m.voices[voice_i];
    if (!v.active || v.sound_id < 0 || v.sound_id >= 32) continue;

    const Sound& s = m.sounds[v.sound_id];
    int sample_count = static_cast<int>(s.samples.size());
    if (sample_count == 0 || v.cursor >= static_cast<float>(sample_count)) {
      v.active = false;
      continue;
    }

    for (int i = 0; i < frames; ++i) {
      int idx = static_cast<int>(v.cursor);
      if (idx >= sample_count) {
        v.active = false;
        break;
      }
      float sample = s.samples[idx];
      v.cursor += v.rate;
      out[i * 2 + 0] += sample * v.left_gain;
      out[i * 2 + 1] += sample * v.right_gain;
    }
  }

  for (int i = 0; i < frames * 2; ++i) {
    out[i] = clamp_sample(out[i]);
  }
}

static void SDLCALL audio_stream_callback(void* userdata, SDL_AudioStream* stream,
                                          int additional_amount, int) {
  Mixer* m = static_cast<Mixer*>(userdata);
  if (!m || additional_amount <= 0) return;

  constexpr int CHANNELS = 2;
  constexpr int MAX_FRAMES_PER_CHUNK = 1024;
  constexpr int FRAME_BYTES = static_cast<int>(sizeof(float)) * CHANNELS;
  float chunk[MAX_FRAMES_PER_CHUNK * CHANNELS];
  int frames_left = (additional_amount + FRAME_BYTES - 1) / FRAME_BYTES;
  while (frames_left > 0) {
    int frames = frames_left < MAX_FRAMES_PER_CHUNK ? frames_left : MAX_FRAMES_PER_CHUNK;
    if (m->enabled) {
      mix_chunk(*m, chunk, frames);
    } else {
      std::fill(chunk, chunk + frames * CHANNELS, 0.0f);
    }
    SDL_PutAudioStreamData(stream, chunk, frames * FRAME_BYTES);
    frames_left -= frames;
  }
}

static bool audio_lock(Mixer& m) {
  return m.stream && SDL_LockAudioStream(m.stream);
}

static void audio_unlock(Mixer& m) {
  if (m.stream) SDL_UnlockAudioStream(m.stream);
}

bool audio_init(Mixer& m) {
  m = {};
  bool already_audio = (SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) != 0;
  if (!already_audio) {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
      log_warn("audio init failed: %s", SDL_GetError());
      return false;
    }
    m.owns_audio_subsystem = true;
  }

  SDL_AudioSpec spec{};
  spec.format = SDL_AUDIO_F32;
  spec.channels = 2;
  spec.freq = AUDIO_SAMPLE_RATE;
  m.stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                       &spec, audio_stream_callback, &m);
  if (!m.stream) {
    log_warn("audio device open failed: %s", SDL_GetError());
    if (m.owns_audio_subsystem) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    m.owns_audio_subsystem = false;
    return false;
  }

  m.enabled = true;
  if (!SDL_ResumeAudioStreamDevice(m.stream)) {
    log_warn("audio device resume failed: %s", SDL_GetError());
    SDL_DestroyAudioStream(m.stream);
    m.stream = nullptr;
    m.enabled = false;
    if (m.owns_audio_subsystem) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    m.owns_audio_subsystem = false;
    return false;
  }
  log_info("audio mixer enabled");
  return true;
}

void audio_shutdown(Mixer& m) {
  if (audio_lock(m)) {
    m.enabled = false;
    std::memset(m.voices, 0, sizeof(m.voices));
    audio_unlock(m);
  }
  if (m.stream) {
    SDL_DestroyAudioStream(m.stream);
    m.stream = nullptr;
  }
  if (m.owns_audio_subsystem) {
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    m.owns_audio_subsystem = false;
  }
}

void audio_register(Mixer& m, int sound_id, Sound s) {
  if (sound_id < 0 || sound_id >= 32) return;
  bool locked = audio_lock(m);
  if (m.stream && !locked) return;
  m.sounds[sound_id] = std::move(s);
  if (locked) audio_unlock(m);
}

void audio_set_listener(Mixer& m, Vec3 pos, float yaw) {
  bool locked = audio_lock(m);
  if (m.stream && !locked) return;
  m.listener_pos = pos;
  m.listener_yaw = yaw;
  if (locked) audio_unlock(m);
}

// Small random playback-rate spread so repeated plays of the same synthesized
// buffer (every rifle shot is byte-identical) don't sound machine-stamped.
static float pitch_variation(Mixer& m) {
  m.pitch_rng = m.pitch_rng * 1664525u + 1013904223u;
  float unit = static_cast<float>((m.pitch_rng >> 8) & 0xffff) / 65535.0f;
  return 0.94f + unit * 0.12f;
}

static void add_voice(Mixer& m, int sound_id, float left_gain, float right_gain) {
  if (!m.enabled || sound_id < 0 || sound_id >= 32 || m.sounds[sound_id].samples.empty()) return;
  int slot = -1;
  uint32_t oldest = UINT32_MAX;
  for (int i = 0; i < AUDIO_MAX_VOICES; ++i) {
    if (!m.voices[i].active) {
      slot = i;
      break;
    }
    if (m.voices[i].started < oldest) {
      oldest = m.voices[i].started;
      slot = i;
    }
  }
  if (slot < 0) return;
  Voice& v = m.voices[slot];
  v.active = true;
  v.sound_id = sound_id;
  v.cursor = 0.0f;
  v.rate = pitch_variation(m);
  v.left_gain = clampf(left_gain, 0.0f, 2.0f);
  v.right_gain = clampf(right_gain, 0.0f, 2.0f);
  v.started = ++m.voice_clock;
}

void audio_play(Mixer& m, int sound_id, float gain) {
  bool locked = audio_lock(m);
  if (m.stream && !locked) return;
  add_voice(m, sound_id, gain, gain);
  if (locked) audio_unlock(m);
}

void audio_play_3d(Mixer& m, int sound_id, Vec3 pos, float gain) {
  bool locked = audio_lock(m);
  if (m.stream && !locked) return;
  float left = gain;
  float right = gain;
  audio_pan(m.listener_pos, m.listener_yaw, pos, &left, &right);
  add_voice(m, sound_id, left * gain, right * gain);
  if (locked) audio_unlock(m);
}

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

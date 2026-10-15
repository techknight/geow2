#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace geow2 {

// The sound output: the extracted banks (cache/audio/<bank>/<wave>.wav, 16-bit
// PCM, 48 kHz) mixed into one SDL stereo stream. Voices have a gain, a pitch
// (playback rate ratio), a pan and may loop.
class AudioMixer {
 public:
  ~AudioMixer();
  // Opens the default output; false (and silence) when there is none.
  bool init();
  // Loads every wave of every bank under `dir`; returns the waves loaded.
  size_t load_banks(const std::filesystem::path& dir);

  struct Wave {
    std::vector<float> samples;  // interleaved, -1 .. 1
    int channels = 1;
    int rate = 48000;
  };
  const Wave* find(const std::string& bank, const std::string& wave) const;

  // Starts a voice; returns its handle (0 when the wave is missing).
  uint32_t play(const Wave* w, float gain, float pitch, float pan, bool loop);
  void stop(uint32_t voice);
  void set_gain(uint32_t voice, float gain);
  void set_pitch(uint32_t voice, float pitch);
  bool playing(uint32_t voice) const;
  void stop_all();

 private:
  struct Voice {
    uint32_t id = 0;
    const Wave* wave = nullptr;
    double pos = 0.0;
    float gain = 1.0f, pitch = 1.0f, pan = 0.0f;
    bool loop = false;
  };
  static void SDLCALL feed(void* self, SDL_AudioStream* stream, int additional, int total);
  void mix(float* out, int frames);

  SDL_AudioStream* stream_ = nullptr;
  std::map<std::string, Wave> waves_;  // "bank/wave"
  mutable std::mutex lock_;
  std::vector<Voice> voices_;
  uint32_t next_id_ = 1;
  std::vector<float> buffer_;
};

}  // namespace geow2

#pragma once

#include <vector>

struct SDL_AudioStream;

namespace geow2::audio {

class AudioEngine;

// The audio output: an SDL3 stream of 6-channel float at 48 kHz, the
// engine's 5.1 mix in SDL's 5.1 order (FL FR C LFE BL BR, which is the
// original's FL FR C LFE SL SR). SDL folds it down to the device's layout.
// SDL's audio thread pulls the frames from the engine.
class AudioDevice {
 public:
  AudioDevice() = default;
  AudioDevice(const AudioDevice&) = delete;
  AudioDevice& operator=(const AudioDevice&) = delete;
  ~AudioDevice();

  // Opens the default output; false (and silence) when there is none.
  bool open(AudioEngine& engine);
  void close();

 private:
  static void feed(void* self, SDL_AudioStream* stream, int additional, int total);

  AudioEngine* engine_ = nullptr;
  SDL_AudioStream* stream_ = nullptr;
  std::vector<float> buffer_;
};

}  // namespace geow2::audio

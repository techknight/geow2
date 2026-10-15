#include "audio/device.h"

#include <SDL3/SDL.h>

#include "audio/engine.h"

namespace geow2::audio {

AudioDevice::~AudioDevice() { close(); }

bool AudioDevice::open(AudioEngine& engine) {
  close();
  engine_ = &engine;
  const SDL_AudioSpec spec{SDL_AUDIO_F32, 6, kSampleRate};
  stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &AudioDevice::feed, this);
  if (!stream_) {
    SDL_Log("no audio output: %s", SDL_GetError());
    return false;
  }
  SDL_ResumeAudioStreamDevice(stream_);
  return true;
}

void AudioDevice::close() {
  if (stream_) SDL_DestroyAudioStream(stream_);
  stream_ = nullptr;
}

void SDLCALL AudioDevice::feed(void* self, SDL_AudioStream* stream, int additional, int) {
  auto* d = static_cast<AudioDevice*>(self);
  if (additional <= 0 || !d->engine_) return;
  // Whole engine frames: at least what SDL asks for.
  const int bytes_per_frame = int(6 * sizeof(float));
  int frames = (additional + bytes_per_frame - 1) / bytes_per_frame;
  frames = (frames + kFrameSamples - 1) / kFrameSamples * kFrameSamples;
  d->buffer_.resize(size_t(frames) * 6);
  d->engine_->render(d->buffer_.data(), size_t(frames));
  SDL_PutAudioStreamData(stream, d->buffer_.data(), frames * bytes_per_frame);
}

}  // namespace geow2::audio

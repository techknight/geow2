#include "app/audio.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "extract/bytes.h"

namespace geow2 {
namespace {

constexpr int kRate = 48000;  // the banks' rate

// A 16-bit PCM RIFF file as the extractor writes it.
bool read_wav(const std::filesystem::path& p, AudioMixer::Wave& out) {
  Bytes b;
  try {
    b = read_file(p);
  } catch (...) {
    return false;
  }
  if (b.size() < 44 || std::memcmp(b.data(), "RIFF", 4) != 0 || std::memcmp(b.data() + 8, "WAVE", 4) != 0) return false;
  size_t off = 12;
  int channels = 0, rate = 0, bits = 0;
  while (off + 8 <= b.size()) {
    const uint32_t size = uint32_t(b[off + 4]) | (uint32_t(b[off + 5]) << 8) | (uint32_t(b[off + 6]) << 16) |
                          (uint32_t(b[off + 7]) << 24);
    const uint8_t* c = b.data() + off + 8;
    if (off + 8 + size > b.size()) return false;
    if (std::memcmp(b.data() + off, "fmt ", 4) == 0 && size >= 16) {
      channels = le16(c + 2);
      rate = int(uint32_t(c[4]) | (uint32_t(c[5]) << 8) | (uint32_t(c[6]) << 16) | (uint32_t(c[7]) << 24));
      bits = le16(c + 14);
    } else if (std::memcmp(b.data() + off, "data", 4) == 0) {
      if (bits != 16 || channels < 1 || channels > 2) return false;
      out.channels = channels;
      out.rate = rate;
      out.samples.resize(size / 2);
      for (size_t i = 0; i < out.samples.size(); ++i)
        out.samples[i] = float(int16_t(le16(c + 2 * i))) / 32768.0f;
      return true;
    }
    off += 8 + size + (size & 1);
  }
  return false;
}

}  // namespace

AudioMixer::~AudioMixer() {
  if (stream_) SDL_DestroyAudioStream(stream_);
}

bool AudioMixer::init() {
  const SDL_AudioSpec spec{SDL_AUDIO_F32, 2, kRate};
  stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &AudioMixer::feed, this);
  if (!stream_) {
    SDL_Log("no audio output: %s", SDL_GetError());
    return false;
  }
  SDL_ResumeAudioStreamDevice(stream_);
  return true;
}

size_t AudioMixer::load_banks(const std::filesystem::path& dir) {
  namespace fs = std::filesystem;
  size_t n = 0;
  std::error_code ec;
  for (const auto& bank : fs::directory_iterator(dir, ec)) {
    if (!bank.is_directory()) continue;
    for (const auto& f : fs::directory_iterator(bank.path(), ec)) {
      if (f.path().extension() != ".wav") continue;
      Wave w;
      if (!read_wav(f.path(), w)) continue;
      waves_[bank.path().filename().string() + "/" + f.path().stem().string()] = std::move(w);
      ++n;
    }
  }
  return n;
}

const AudioMixer::Wave* AudioMixer::find(const std::string& bank, const std::string& wave) const {
  const auto it = waves_.find(bank + "/" + wave);
  return it == waves_.end() ? nullptr : &it->second;
}

uint32_t AudioMixer::play(const Wave* w, float gain, float pitch, float pan, bool loop) {
  if (!w || !stream_) return 0;
  std::lock_guard<std::mutex> g(lock_);
  Voice v;
  v.id = next_id_++;
  v.wave = w;
  v.gain = gain;
  v.pitch = pitch;
  v.pan = pan;
  v.loop = loop;
  voices_.push_back(v);
  return v.id;
}

void AudioMixer::stop(uint32_t id) {
  std::lock_guard<std::mutex> g(lock_);
  std::erase_if(voices_, [id](const Voice& v) { return v.id == id; });
}

void AudioMixer::stop_all() {
  std::lock_guard<std::mutex> g(lock_);
  voices_.clear();
}

void AudioMixer::set_gain(uint32_t id, float gain) {
  std::lock_guard<std::mutex> g(lock_);
  for (Voice& v : voices_)
    if (v.id == id) v.gain = gain;
}

void AudioMixer::set_pitch(uint32_t id, float pitch) {
  std::lock_guard<std::mutex> g(lock_);
  for (Voice& v : voices_)
    if (v.id == id) v.pitch = pitch;
}

bool AudioMixer::playing(uint32_t id) const {
  std::lock_guard<std::mutex> g(lock_);
  for (const Voice& v : voices_)
    if (v.id == id) return true;
  return false;
}

void SDLCALL AudioMixer::feed(void* self, SDL_AudioStream* stream, int additional, int) {
  auto* m = static_cast<AudioMixer*>(self);
  const int frames = additional / int(2 * sizeof(float));
  if (frames <= 0) return;
  m->buffer_.assign(size_t(frames) * 2, 0.0f);
  m->mix(m->buffer_.data(), frames);
  SDL_PutAudioStreamData(stream, m->buffer_.data(), frames * int(2 * sizeof(float)));
}

void AudioMixer::mix(float* out, int frames) {
  std::lock_guard<std::mutex> g(lock_);
  for (Voice& v : voices_) {
    const Wave& w = *v.wave;
    const size_t len = w.samples.size() / size_t(w.channels);
    if (len == 0) {
      v.wave = nullptr;
      continue;
    }
    // Equal-power pan; linear interpolation between samples for the pitch.
    const float l = v.gain * std::sqrt(0.5f * (1.0f - v.pan)), r = v.gain * std::sqrt(0.5f * (1.0f + v.pan));
    const double step = double(v.pitch) * w.rate / kRate;
    for (int i = 0; i < frames; ++i) {
      if (v.pos >= double(len)) {
        if (!v.loop) {
          v.wave = nullptr;
          break;
        }
        v.pos -= double(len);
      }
      const size_t a = size_t(v.pos), b = a + 1 < len ? a + 1 : (v.loop ? 0 : a);
      const float t = float(v.pos - double(a));
      float sl, sr;
      if (w.channels == 2) {
        sl = w.samples[2 * a] + (w.samples[2 * b] - w.samples[2 * a]) * t;
        sr = w.samples[2 * a + 1] + (w.samples[2 * b + 1] - w.samples[2 * a + 1]) * t;
      } else {
        sl = sr = w.samples[a] + (w.samples[b] - w.samples[a]) * t;
      }
      out[2 * i] += sl * l;
      out[2 * i + 1] += sr * r;
      v.pos += step;
    }
  }
  std::erase_if(voices_, [](const Voice& v) { return v.wave == nullptr; });
}

}  // namespace geow2

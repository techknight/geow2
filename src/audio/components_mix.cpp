// The mixers (Core::Mixer, sub_82103EE0 / sub_821041D8) and the audio
// inputs and outputs that join objects together.

#include <algorithm>
#include <bit>
#include <cfloat>
#include <cmath>

#include "audio/components.h"

namespace geow2::audio {

float core_sqrt(float x) {
  // x * frsqrte(x) refined by two Newton steps, 0 for x <= 0. The
  // hardware estimate is not available; the seed here is the correctly
  // rounded 1/sqrt(x), so results can differ from the original in the last
  // bit.
  float y = x > 0.0f ? float(1.0 / std::sqrt(double(x))) : 0.0f;
  for (int i = 0; i < 2; ++i) {
    float t = y * y;
    t = -std::fma(t, x, -1.0f);
    t = t * y;
    y = std::fma(t, 0.5f, y);
  }
  y = y >= 0.0f ? y : 0.0f;
  const float r = y * x;
  return -x >= 0.0f ? 0.0f : r;
}

namespace {

constexpr float kPi = 3.14159274f;  // dat_82018A50

// The channel-type flag bits of an audio output, by channel count.
uint32_t channel_flag(int channels) {
  switch (channels) {
    case 1: return 0x800;
    case 2: return 0x1000;
    case 4: return 0x2000;
    case 5: return 0x4000;
    default: return 0x8000;
  }
}

// ------------------------------------------------------------ inputs/outputs

// 0x0C MonoAudioInput / 0xB8 SurroundLFEAudioInput [name]: what other
// objects add during a frame is this input's output in the next frame
// (Prepare sub_8210ACE0 / sub_8210B548 -> sub_820FDFB8).
class AudioInputNode : public AudioInput {
 public:
  explicit AudioInputNode(int channels) : channels_(channels) { acc.channels = channels; }
  uint32_t flags() const override { return channels_ == 1 ? 0x44410800u : 0x44418000u; }
  int out_channels() const override { return channels_; }
  void prepare(Object&) override {
    out = acc;
    out.channels = channels_;
    acc.clear();
    active_ = true;
  }
  bool render(Object&) override { return active_; }
  AudioBuffer acc;

 private:
  int channels_;
  bool active_ = false;
};

// 0x0D MonoAudioOutput / 0xB9 SurroundLFEAudioOutput [name, source]: pulls
// the source and adds it to the connected input (sub_820FEFE8).
class AudioOutputNode : public Component {
 public:
  AudioOutputNode(const AobjRecord& r, int channels) : src_(int32_t(r.body[1])), channels_(channels) {}
  uint32_t flags() const override { return channels_ == 1 ? 0x24800800u : 0x24808000u; }
  void generate(Object& o) override {
    if (src_ == -1 || !o.pull(src_)) return;
    if (!dest) return;
    const AudioBuffer& in = o.comps[size_t(src_)]->out;
    const int n = std::min(std::min(in.channels, channels_), dest->channels);
    for (int c = 0; c < n; ++c)
      for (int i = 0; i < kFrameSamples; ++i) dest->ch[size_t(c)][size_t(i)] = dest->ch[size_t(c)][size_t(i)] + in.ch[size_t(c)][size_t(i)];
  }
  AudioBuffer* dest = nullptr;

 private:
  int32_t src_;
  int channels_;
};

// ------------------------------------------------------------------ mixers

// The speaker layout of the surround panner (sub_820E9EA0): five speakers at
// these azimuths (units of pi, +0xFAEC), radius 2.0 (+0xFAE8), surround on
// with rear factor S = 1 (+0xFB30, +0xFB2C; their only setter,
// sub_820E98E8, has no caller).
struct Speakers {
  float az[5];
  float share[5];  // the non-directional share P (sub_820E95E8)
  float radius = 2.0f;
  float s = 1.0f;

  Speakers() {
    const uint32_t bits[5] = {0xBF2AAAAB, 0xBE2AAAAB, 0x00000000, 0x3E2AAAAB, 0x3F2AAAAB};
    for (int i = 0; i < 5; ++i) az[i] = std::bit_cast<float>(bits[i]);
    float sh[5] = {};
    for (int i = 0; i < 5; ++i) {
      if (i == 0) sh[0] = std::fma(az[0] + 1.0f, 0.5f, sh[0]);
      else sh[i] = std::fma(az[i] - az[i - 1], 0.25f, sh[i]);
      if (i == 4) sh[i] = -std::fma(az[i], 0.5f, -0.5f) + sh[i];
      else sh[i] = std::fma(az[i + 1] - az[i], 0.25f, sh[i]);
    }
    for (int i = 0; i < 5; ++i) share[i] = core_sqrt(sh[i]);
  }
};

const Speakers& speakers() {
  static const Speakers s;
  return s;
}

// Azimuths outside [-1, 1] wrap by whole turns.
float wrap_azimuth(float x) {
  int k;
  if (x < -1.0f) k = int(x) - 1;
  else if (x > 1.0f) k = int(x) + 1;
  else return x;
  const int i = k / 2;
  return -std::fma(float(i), 2.0f, -x);
}

// Pairwise panning of one azimuth onto the speakers. Only gains of the
// speaker pair are written (the others keep what they had unless the
// caller cleared them), and an azimuth exactly on the last speaker writes
// nothing.
void pan(float azm, float g[5]) {
  const Speakers& sp = speakers();
  const float* a = sp.az;
  if (azm < a[0]) {
    const float t = ((azm + 2.0f) - a[4]) / ((a[0] + 2.0f) - a[4]);
    g[0] = core_sqrt(t);
    g[4] = core_sqrt(1.0f - t);
  } else if (azm > a[4]) {
    const float t = (azm - a[4]) / ((a[0] + 2.0f) - a[4]);
    g[0] = core_sqrt(t);
    g[4] = core_sqrt(1.0f - t);
  } else {
    for (int i = 1; i < 5; ++i) {
      if (azm < a[i]) {
        const float t = (azm - a[i - 1]) / (a[i] - a[i - 1]);
        g[i] = core_sqrt(t);
        g[i - 1] = core_sqrt(1.0f - t);
        break;
      }
    }
  }
}

float clamp01(float w) {
  if (w < 0.0f) return 0.0f;
  return w > 1.0f ? 1.0f : w;
}

class Mixer : public Component {
 public:
  Mixer(const AobjRecord& r, int header_triples, int in_ch, int out_ch) : in_ch_(in_ch), out_ch_(out_ch) {
    const size_t h = 1 + 3 * size_t(header_triples);
    for (int i = 0; i < header_triples; ++i) header_.push_back(triple_at(r, 1 + 3 * size_t(i)));
    const uint32_t e = r.body.at(h), n = r.body.at(h + 1);
    valid_ = e <= 8;  // InitState logs an error and builds no Core::Mixer
    for (uint32_t i = 0; i < e; ++i) effects_.push_back(int32_t(r.body.at(h + 2 + i)));
    const size_t at = h + 2 + e;
    for (uint32_t i = 0; i < n; ++i) inputs_.push_back({int32_t(r.body.at(at + 4 * i)), triple_at(r, at + 4 * i + 1)});
    cur_.assign(inputs_.size() * size_t(in_ch) * size_t(out_ch), 0.0f);
    prev_ = cur_;
    active_.assign(inputs_.size(), false);
  }
  uint32_t flags() const override { return 0x10010000u | channel_flag(out_ch_); }
  int out_channels() const override { return out_ch_; }

  // ConfigureAudio (slot 17): an input connects only when its source has an
  // audio output of the mixer's input type.
  void init_state(Object& o) override {
    for (Input& in : inputs_) {
      in.connected = false;
      if (in.src < 0 || size_t(in.src) >= o.comps.size()) continue;
      const uint32_t f = o.comps[size_t(in.src)]->flags();
      in.connected = (f & kFlagAudioOut) && (f & channel_flag(in_ch_));
    }
  }

  // sub_821041D8.
  bool render(Object& o) override {
    if (stamp_ == o.frame()) return active_now_;
    if (!valid_) return false;
    bool any = false;
    for (size_t i = 0; i < inputs_.size(); ++i) {
      active_[i] = inputs_[i].connected && o.pull(inputs_[i].src);
      any = any || active_[i];
    }
    out.clear();
    out.channels = out_ch_;
    // Mixing happens only when the mixer was active in the previous frame.
    if (active_now_) {
      for (size_t i = 0; i < inputs_.size(); ++i) {
        if (!active_[i]) continue;
        const AudioBuffer& in = o.comps[size_t(inputs_[i].src)]->out;
        for (int c = 0; c < in_ch_; ++c)
          for (int k = 0; k < out_ch_; ++k) {
            const size_t g = (size_t(in_ch_) * i + size_t(c)) * size_t(out_ch_) + size_t(k);
            mix(in.ch[size_t(c)].data(), out.ch[size_t(k)].data(), prev_[g], cur_[g], o);
          }
      }
      for (int32_t e : effects_)
        if (e >= 0 && size_t(e) < o.comps.size()) o.comps[size_t(e)]->process_effect(o, out);
      stamp_ = o.frame();
    }
    active_now_ = any;
    if (o.sub == kUpdatePeriod - 1) prev_ = cur_;
    return active_now_;
  }

 protected:
  struct Input {
    int32_t src;
    Triple gain;
    bool connected = false;
  };

  static void mix(const float* in, float* out, float p, float a, const Object& o) {
    if (p == a) {
      if (a == 1.0f) {
        for (int n = 0; n < kFrameSamples; ++n) out[n] = in[n] + out[n];
      } else if (a != 0.0f) {
        for (int n = 0; n < kFrameSamples; ++n) out[n] = std::fma(in[n], a, out[n]);
      }
      return;
    }
    // A gain change ramps linearly across the frame from the point of the
    // control period this frame starts at (sub_82103BD0).
    const float d = a - p;
    const float g0 = std::fma(o.ramp_start, d, p);
    const float g1 = std::fma(o.ramp_span + o.ramp_start, d, p);
    const float inc = (g1 - g0) * (1.0f / 256.0f);
    for (int n = 0; n < kFrameSamples; ++n) out[n] = std::fma(in[n], std::fma(float(n), inc, g0), out[n]);
  }

  float& gain(size_t input, int c, int k) {
    return cur_[(size_t(in_ch_) * input + size_t(c)) * size_t(out_ch_) + size_t(k)];
  }

  int in_ch_, out_ch_;
  std::vector<Triple> header_;
  std::vector<int32_t> effects_;
  std::vector<Input> inputs_;
  std::vector<float> cur_, prev_;
  std::vector<bool> active_;
  bool valid_ = true, active_now_ = false;
  uint32_t stamp_ = UINT32_MAX;
};

// 0x3D MonoToMono, 0x45 SurroundToSurround, 0xB7 SurroundLFEToSurroundLFE
// [gain]: a diagonal at input gain * master (sub_8210E2C8, sub_82123C90,
// sub_82124908).
class DiagonalMixer : public Mixer {
 public:
  DiagonalMixer(const AobjRecord& r, int ch) : Mixer(r, 1, ch, ch) {}
  void update(Object& o) override {
    const float m = o.read(header_[0]);
    for (size_t i = 0; i < inputs_.size(); ++i) {
      const float g = o.read(inputs_[i].gain) * m;
      for (int c = 0; c < in_ch_; ++c)
        for (int k = 0; k < out_ch_; ++k) gain(i, c, k) = c == k ? g : 0.0f;
    }
  }
};

// 0x3E MonoToStereo [gain, pan] (sub_8210E880).
class MonoToStereo : public Mixer {
 public:
  explicit MonoToStereo(const AobjRecord& r) : Mixer(r, 2, 1, 2) {}
  void init_state(Object& o) override {
    Mixer::init_state(o);
    p_ = 0.0f;
    l_ = r_ = std::bit_cast<float>(0x3F34FDF4u);  // dat_82016424, 0.707
  }
  void update(Object& o) override {
    const float m = o.read(header_[0]);
    const float p = o.read(header_[1]);
    if (p != p_) {
      p_ = p;
      l_ = core_sqrt((1.0f - p) * 0.5f);
      r_ = core_sqrt((p + 1.0f) * 0.5f);
    }
    for (size_t i = 0; i < inputs_.size(); ++i) {
      const float g = o.read(inputs_[i].gain) * m;
      gain(i, 0, 0) = l_ * g;
      gain(i, 0, 1) = r_ * g;
    }
  }

 private:
  float p_ = 0.0f, l_ = 0.0f, r_ = 0.0f;
};

// 0x40 StereoToMono [balance] (sub_82115DA8): no master gain.
class StereoToMono : public Mixer {
 public:
  explicit StereoToMono(const AobjRecord& r) : Mixer(r, 1, 2, 1) {}
  void init_state(Object& o) override {
    Mixer::init_state(o);
    b_ = 0.0f;
    l_ = r_ = std::bit_cast<float>(0x3F3504F3u);
  }
  void update(Object& o) override {
    float b = o.read(header_[0]);
    if (!(b >= -1.0f)) b = -1.0f;
    else if (b > 1.0f) b = 1.0f;
    if (b != b_) {
      b_ = b;
      const float h = b * 0.5f;
      l_ = core_sqrt(0.5f - h);
      r_ = core_sqrt(h + 0.5f);
    }
    for (size_t i = 0; i < inputs_.size(); ++i) {
      const float g = o.read(inputs_[i].gain);
      gain(i, 0, 0) = l_ * g;
      gain(i, 1, 0) = r_ * g;
    }
  }

 private:
  float b_ = 0.0f, l_ = 0.0f, r_ = 0.0f;
};

// 0x41 StereoToStereo [gain, left position, right position] (sub_821164A0).
class StereoToStereo : public Mixer {
 public:
  explicit StereoToStereo(const AobjRecord& r) : Mixer(r, 3, 2, 2) {}
  void init_state(Object& o) override {
    Mixer::init_state(o);
    pl_ = -0.5f;
    pr_ = 0.5f;
    l0_ = 1.0f, l1_ = 0.0f, r0_ = 0.0f, r1_ = 1.0f;
  }
  void update(Object& o) override {
    const float m = o.read(header_[0]);
    const float pl = o.read(header_[1]);
    const float pr = o.read(header_[2]);
    if (pl != pl_) {
      pl_ = pl;
      l0_ = core_sqrt((1.0f - pl) * 0.5f);
      l1_ = core_sqrt((pl + 1.0f) * 0.5f);
    }
    if (pr != pr_) {
      pr_ = pr;
      r0_ = core_sqrt((1.0f - pr) * 0.5f);
      r1_ = core_sqrt((pr + 1.0f) * 0.5f);
    }
    for (size_t i = 0; i < inputs_.size(); ++i) {
      const float g = o.read(inputs_[i].gain);
      gain(i, 0, 0) = (l0_ * g) * m;
      gain(i, 0, 1) = (l1_ * g) * m;
      gain(i, 1, 0) = (g * m) * r0_;
      gain(i, 1, 1) = (r1_ * g) * m;
    }
  }

 private:
  float pl_ = 0, pr_ = 0, l0_ = 1, l1_ = 0, r0_ = 0, r1_ = 1;
};

// 0xB6 SurroundToSurroundLFE [gain, LFE] (sub_82124290): FL FR C SL SR to
// the same channels at input gain * master, and every input channel to the
// LFE at the raw LFE value.
class SurroundToSurroundLfe : public Mixer {
 public:
  explicit SurroundToSurroundLfe(const AobjRecord& r) : Mixer(r, 2, 5, 6) {}
  void update(Object& o) override {
    const float m = o.read(header_[0]);
    const float lfe = o.read(header_[1]);
    static const int map[5] = {0, 1, 2, 4, 5};
    for (size_t i = 0; i < inputs_.size(); ++i) {
      const float g = o.read(inputs_[i].gain) * m;
      for (int c = 0; c < 5; ++c)
        for (int k = 0; k < 6; ++k) gain(i, c, k) = k == 3 ? lfe : (k == map[c] ? g : 0.0f);
    }
  }
};

// 0xBA StereoToQuad [unused, 2 x 4 matrix] (sub_82116D10): no master.
class StereoToQuad : public Mixer {
 public:
  explicit StereoToQuad(const AobjRecord& r) : Mixer(r, 9, 2, 4) {}
  void update(Object& o) override {
    float t[9];
    for (int k = 1; k < 9; ++k) t[k] = o.read(header_[size_t(k)]);
    for (size_t i = 0; i < inputs_.size(); ++i) {
      const float g = o.read(inputs_[i].gain);
      for (int k = 0; k < 4; ++k) {
        gain(i, 0, k) = g * t[1 + k];
        gain(i, 1, k) = g * t[5 + k];
      }
    }
  }
};

// One panned channel of a surround mixer: its cached position and the
// speaker gains it keeps between updates.
struct PanState {
  float az = -2.0f, second = 2.0f, w = 1.0f;
  float near_ = 0.0f, direct = 1.0f;  // sqrt(1 - w), sqrt(w)
  float g[5] = {};
};

// 0x42 StereoToSurround [gain, azL, azR, angL, angR, distL, distR]
// (sub_82117678) and 0xBB QuadToSurround [gain, 4 x (az, unused, dist)]
// (sub_8211A070): the weights come from the previously cached distance
// (kept), and old pan gains are not cleared.
class SurroundPanner : public Mixer {
 public:
  SurroundPanner(const AobjRecord& r, bool quad) : Mixer(r, quad ? 13 : 7, quad ? 4 : 2, 5), quad_(quad) {
    pan_.resize(size_t(in_ch_));
  }
  void init_state(Object& o) override {
    Mixer::init_state(o);
    for (PanState& p : pan_) p = PanState{};
  }
  void update(Object& o) override {
    const Speakers& sp = speakers();
    const float m = o.read(header_[0]);
    float rear[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    for (int c = 0; c < in_ch_; ++c) {
      PanState& p = pan_[size_t(c)];
      float az, second, dist;
      if (quad_) {
        az = o.read(header_[1 + 3 * size_t(c)]);
        second = o.read(header_[2 + 3 * size_t(c)]);
        dist = o.read(header_[3 + 3 * size_t(c)]);
      } else {
        az = o.read(header_[1 + size_t(c)]);
        second = o.read(header_[3 + size_t(c)]);
        dist = o.read(header_[5 + size_t(c)]);
      }
      az = wrap_azimuth(az);
      if (quad_) second = wrap_azimuth(second);
      const float inv_r = 1.0f / sp.radius;
      float w = clamp01(inv_r * dist);
      if (!quad_) {
        w = float(std::cos(double(kPi * second))) * w;
        // Rear factor 1 - |az| (1 - S); with surround on there is no fold.
        rear[c] = -std::fma(std::fabs(az), 1.0f - sp.s, -1.0f);
      }
      const bool moved = az != p.az || second != p.second || w != p.w;
      if (w != p.w) {
        p.near_ = core_sqrt(1.0f - p.w);
        p.direct = core_sqrt(p.w);
      }
      if (moved) pan(az, p.g);
      p.az = az, p.second = second, p.w = w;
    }
    // Speaker -> output channel: [3, 0, 2, 1, 4].
    static const int speaker_of[5] = {1, 3, 2, 0, 4};
    for (size_t i = 0; i < inputs_.size(); ++i) {
      const float gi = o.read(inputs_[i].gain);
      for (int c = 0; c < in_ch_; ++c) {
        const PanState& p = pan_[size_t(c)];
        const float g = quad_ ? gi * m : (gi * rear[c]) * m;
        for (int k = 0; k < 5; ++k) {
          const int s = speaker_of[k];
          const float v = std::fma(p.g[s], p.direct, p.near_ * sp.share[s]);
          gain(i, c, k) = v * g;
        }
      }
    }
  }

 private:
  bool quad_;
  std::vector<PanState> pan_;
};

// 0xB5 MonoToSurroundLFE [gain, az, unused, dist, LFE] (sub_82124F18): the
// pan gains are cleared first, the weights use the new distance, and the
// LFE output is the raw LFE value.
class MonoToSurroundLfe : public Mixer {
 public:
  explicit MonoToSurroundLfe(const AobjRecord& r) : Mixer(r, 5, 1, 6) {}
  void init_state(Object& o) override {
    Mixer::init_state(o);
    az_ = second_ = w_ = -1.0f;
    for (float& x : g_) x = 0.0f;
    near_ = 0.0f;
    direct_ = 1.0f;
  }
  void update(Object& o) override {
    const Speakers& sp = speakers();
    const float m = o.read(header_[0]);
    const float az = wrap_azimuth(o.read(header_[1]));
    const float second = wrap_azimuth(o.read(header_[2]));
    const float dist = o.read(header_[3]);
    const float lfe = o.read(header_[4]);
    const float w = clamp01(dist / sp.radius);
    if (az != az_ || second != second_ || w != w_) {
      near_ = core_sqrt(1.0f - w);
      direct_ = core_sqrt(w);
      for (float& x : g_) x = 0.0f;
      pan(az, g_);
      az_ = az, second_ = second, w_ = w;
    }
    for (size_t i = 0; i < inputs_.size(); ++i) {
      const float g = o.read(inputs_[i].gain) * m;
      // Outputs FL FR C from speakers 1, 3, 2; SL, SR from speakers 0 and 4
      // (both with the share of speaker 4); LFE raw.
      gain(i, 0, 0) = (g_[1] * direct_ + sp.share[1] * near_) * g;
      gain(i, 0, 1) = (g_[3] * direct_ + sp.share[3] * near_) * g;
      gain(i, 0, 2) = (g_[2] * direct_ + sp.share[2] * near_) * g;
      gain(i, 0, 3) = lfe;
      gain(i, 0, 4) = (g_[0] * direct_ + sp.share[4] * near_) * g;
      gain(i, 0, 5) = (g_[4] * direct_ + sp.share[4] * near_) * g;
    }
  }

 private:
  float az_ = -1, second_ = -1, w_ = -1, near_ = 0, direct_ = 1;
  float g_[5] = {};
};

}  // namespace

bool connect_audio_output(Component& output, AudioBuffer* destination) {
  if (auto* o = dynamic_cast<AudioOutputNode*>(&output)) {
    o->dest = destination;
    return true;
  }
  return false;
}

AudioBuffer* audio_input_accumulator(Component& input) {
  if (auto* i = dynamic_cast<AudioInputNode*>(&input)) return &i->acc;
  return nullptr;
}

std::unique_ptr<Component> make_mixer_component(const AobjRecord& r, const Aobj&) {
  switch (r.type) {
    case 0x0C: return std::make_unique<AudioInputNode>(1);
    case 0xB8: return std::make_unique<AudioInputNode>(6);
    case 0x0D: return std::make_unique<AudioOutputNode>(r, 1);
    case 0xB9: return std::make_unique<AudioOutputNode>(r, 6);
    case 0x3D: return std::make_unique<DiagonalMixer>(r, 1);
    case 0x45: return std::make_unique<DiagonalMixer>(r, 5);
    case 0xB7: return std::make_unique<DiagonalMixer>(r, 6);
    case 0x3E: return std::make_unique<MonoToStereo>(r);
    case 0x40: return std::make_unique<StereoToMono>(r);
    case 0x41: return std::make_unique<StereoToStereo>(r);
    case 0x42: return std::make_unique<SurroundPanner>(r, false);
    case 0xBB: return std::make_unique<SurroundPanner>(r, true);
    case 0xB5: return std::make_unique<MonoToSurroundLfe>(r);
    case 0xB6: return std::make_unique<SurroundToSurroundLfe>(r);
    case 0xBA: return std::make_unique<StereoToQuad>(r);
    default: return nullptr;
  }
}

}  // namespace geow2::audio

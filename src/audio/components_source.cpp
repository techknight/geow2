// The sound sources: 0x34 MonoOneShotSound, 0x35 StereoOneShotSound,
// 0x38 MonoLoopingSound, 0x39 StereoLoopingSound. Each voice is a player
// (by the wave's format) inside a voice node that applies the gain; a
// one-shot with several voices sums them in a Core::Mixer.
//
// The waves reach the port as 16-bit PCM. PCM waves (format 3) play as the
// original's PCM player does. XMA waves (format 8) were decoded by the
// hardware in the original; the port plays their decoded samples with the
// traced parts of the XMA player (no one-sample delay when not resampling,
// truncated block lengths, no anti-alias filter in stereo, no Finished at a
// loop) and without the hardware's decode latency (DIFFERENCES.md).

#include <cfloat>
#include <cmath>

#include "audio/components.h"
#include "audio/waves.h"

namespace geow2::audio {

namespace {

constexpr float kPi = 3.14159274f;           // dat_82018A50
constexpr float kInvRate = 2.08333331e-05f;  // dat_82196B54
constexpr float kInvShort = 1.0f / 32768.0f; // dat_82018A78
constexpr float kLn10 = 2.30258512f;         // dat_82018A6C

// One player: a wave, a position and the resampler's state.
class Player {
 public:
  Player(const Wave* w, bool loop, bool bypass) : wave_(w), loop_(loop), bypass_(bypass) {
    xma_ = w && w->format != 3;
    len_ = w ? uint32_t(w->frames()) : 0;
    chans_ = w ? int(w->channels) : 1;
  }

  uint32_t length() const { return len_; }

  // vt+0x24: the step for this frame's rate, clamped [1/256, 4]
  // (sub_82102700, sub_820F8EE8).
  void set_rate(float rate) {
    if (!wave_) return;
    const float t = (float(wave_->rate) * kInvRate) * rate;
    target_ = t > 4.0f ? 4.0f : (t >= 1.0f / 256.0f ? t : 1.0f / 256.0f);
  }

  // Seek (sub_821026F0); XMA waves have no seek table in the shipped banks,
  // so their audio always restarts from the beginning (sub_820F90B0).
  void seek(uint32_t pos) {
    pos_ = xma_ ? 0 : pos;
    from_ = to_;
  }

  // Start (sub_821029A0 -> sub_82102570 for PCM; the XMA start leaves the
  // held sample and the filter as they were).
  void start() {
    playing_ = true;
    if (!xma_) {
      for (int c = 0; c < 2; ++c) held_[c] = x1_[c] = y1_[c] = 0.0f;
      coefficients(0.005f);
    }
  }
  void stop() { playing_ = false; }
  bool playing() const { return playing_; }

  // Progress for the GetControl outputs: pos / len (sub_82102690).
  float progress() const { return len_ ? float(pos_) / float(len_) : 0.0f; }

  // Renders one block into `out`; true when it produced audio. `finished`
  // is called when the wave ends (and at each PCM loop wrap).
  template <class F>
  bool render(AudioBuffer& out, const Object& o, F&& finished) {
    for (int c = 0; c < chans_; ++c) out.ch[size_t(c)].fill(0.0f);
    if (!wave_ || len_ == 0) return false;
    // The step ramps across the control period (PCM); the XMA player's
    // own cadence comes from the renderer thread and is not modelled.
    const float step = xma_ ? target_ : std::fma(to_ - from_, o.ramp_start, from_);
    if (pos_ >= len_) {
      if (xma_ && !loop_ && playing_) {
        // The XMA one-shot finishes on the render after its data ran out.
        playing_ = false;
        finished();
      }
      return false;
    }
    if (!bypass_ && (to_ != from_ || o.sub == 0)) coefficients(step > 0.5f ? 0.5f / step : 1.0f);
    for (int c = 0; c < chans_; ++c) render_channel(out.ch[size_t(c)].data(), c, step);
    pos_ += advance_;
    if (pos_ >= len_) {
      if (!xma_) finished();
      if (loop_) pos_ -= len_;
      else if (!xma_) {
        pos_ = len_;
        playing_ = false;
      }
    }
    if (!xma_ && o.sub == kUpdatePeriod - 1) {
      from_ = to_;
      to_ = target_;
    }
    return true;
  }

 private:
  float sample(int c, int64_t i) const {
    if (i < 0) return 0.0f;  // the original reads the bytes before the wave
    return float(wave_->samples[size_t(i) * size_t(chans_) + size_t(c)]) * kInvShort;
  }

  // The one-pole anti-alias filter coefficients (sub_82103A90): atan where a
  // bilinear design would use tan, kept.
  void coefficients(float fc) {
    const float k = float(std::atan(double(kPi * fc)));
    a_ = -((1.0f / (k + 1.0f)) * (k - 1.0f));
    b_ = (1.0f - a_) * 0.5f;
  }

  void render_channel(float* out, int c, float step) {
    const int64_t pos = pos_;
    if (bypass_) {
      advance_ = kFrameSamples;
      if (xma_) {
        // No one-sample delay.
        for (int k = 0; k < kFrameSamples; ++k) {
          int64_t i = pos + k;
          if (i >= len_) {
            if (!loop_) break;
            i -= len_;
          }
          out[k] = sample(c, i);
        }
        return;
      }
      // PCM: one sample late (sub_821029B8).
      out[0] = held_[c];
      if (pos + 255 < int64_t(len_)) {
        for (int k = 1; k < kFrameSamples; ++k) out[k] = sample(c, pos + k - 1);
        held_[c] = sample(c, pos + 255);
      } else {
        const int64_t rem = int64_t(len_) - pos;
        for (int64_t k = 1; k <= rem; ++k) out[k] = sample(c, pos + k - 1);
        if (loop_) {
          // Quirk kept: the wrap repeats sample 254 - rem and skips the next.
          for (int64_t k = rem + 1; k < kFrameSamples; ++k) out[k] = sample(c, k - rem - 1);
          held_[c] = sample(c, 254 - rem);
        } else {
          held_[c] = 0.0f;
        }
      }
      return;
    }
    // Resampling: n source samples per block, then linear interpolation.
    const int n = xma_ ? int(256.0f * step) : int(std::fma(256.0f, step, 0.5f));
    advance_ = uint32_t(n);
    float t[1024 + 2];
    t[0] = held_[c];
    for (int k = 1; k <= n; ++k) {
      int64_t i = pos + k - 1;
      if (i >= int64_t(len_)) {
        if (!loop_) {
          t[k] = 0.0f;
          continue;
        }
        i -= len_;
      }
      t[k] = sample(c, i);
    }
    if (!(xma_ && chans_ == 2)) {
      // sub_8210C4C8; the stereo XMA path has no filter.
      for (int k = 0; k < n; ++k) {
        const float y = std::fma(t[k] + x1_[c], b_, y1_[c] * a_);
        x1_[c] = t[k];
        y1_[c] = y;
        t[k] = y;
      }
      t[n] = std::fma(t[n] + x1_[c], b_, y1_[c] * a_);
    }
    if (step == 1.0f) {
      for (int k = 0; k < kFrameSamples; ++k) out[k] = t[k];
    } else {
      for (int k = 0; k < kFrameSamples; ++k) {
        const float p = float(k * n) / 256.0f;
        const int j = int(p);
        const float f = p - float(j);
        out[k] = std::fma(t[j + 1] - t[j], f, t[j]);
      }
    }
    held_[c] = t[n];
  }

  const Wave* wave_;
  bool loop_, bypass_, xma_ = false, playing_ = false;
  uint32_t len_ = 0, pos_ = 0, advance_ = kFrameSamples;
  int chans_ = 1;
  float target_ = 1.0f, from_ = 1.0f, to_ = 1.0f;
  float held_[2] = {}, x1_[2] = {}, y1_[2] = {}, a_ = 0.0f, b_ = 0.0f;
};

// A voice node (sub_82101F78 / sub_82102010): a player and its gain.
struct Voice {
  explicit Voice(Player p, bool one_shot) : player(std::move(p)), one_shot(one_shot) {}
  Player player;
  bool one_shot;
  bool started = false;      // node+0x29
  bool played_last = false;  // node+0xC
  float rate = 1.0f;         // node+0x38
  float g2c = 0.0f, g30 = 0.0f, g34 = 0.0f;
  AudioBuffer out;

  template <class F>
  bool render(const Object& o, F&& finished) {
    player.set_rate(rate);
    bool active = false;
    if (started) {
      active = player.render(out, o, finished);
      if (!player.playing()) started = false;
    } else {
      out.clear();
    }
    // The gain: one-shots step to the target at the start of a control
    // period; loops interpolate across it. Nothing is applied when either
    // end is exactly 1.
    float start, end;
    if (one_shot) {
      start = o.sub == 0 ? g30 : g34;
      end = g34;
    } else {
      start = g2c;
      end = std::fma(g34 - g30, o.ramp_start, g30);
    }
    if (active && start != 1.0f && end != 1.0f) {
      for (int c = 0; c < out.channels; ++c) {
        float* s = out.ch[size_t(c)].data();
        if (start == end) {
          for (int n = 0; n < kFrameSamples; ++n) s[n] = s[n] * start;
        } else {
          const float d = (end - start) * (1.0f / 256.0f);
          for (int n = 0; n < kFrameSamples; ++n) s[n] = s[n] * std::fma(float(n), d, start);
        }
      }
    }
    g2c = end;
    if (o.sub == kUpdatePeriod - 1) g30 = g34;
    played_last = active;
    return active;
  }

  // node vt+0x14 then vt+0x20 (sub_820F3398).
  void play(uint32_t at) {
    g30 = 0.0f;
    player.seek(at);
    g2c = 0.0f;
    player.start();
    started = true;
  }
  void stop() {
    player.stop();
    started = false;
  }
};

class Source : public Component {
 public:
  Source(const AobjRecord& r, const Aobj& def, const WaveStore* waves) {
    type = r.type;
    one_shot_ = r.type == 0x34 || r.type == 0x35;
    stereo_ = r.type == 0x35 || r.type == 0x39;
    const std::string bank = def.string_at(r.body[1]);
    const std::string wave = def.string_at(r.body[2]);
    wave_ = waves ? waves->find(bank, wave) : nullptr;
    if (wave_ && int(wave_->channels) != (stereo_ ? 2 : 1)) wave_ = nullptr;
    if (one_shot_) {
      voices_n_ = std::max<uint32_t>(r.body[3], 1);
      a_ = triple_at(r, 4), b_ = triple_at(r, 7), c_ = triple_at(r, 10), d_ = triple_at(r, 13);
      finished_ = int32_t(r.body[16]);
    } else {
      voices_n_ = 1;
      a_ = triple_at(r, 3), b_ = triple_at(r, 6), c_ = triple_at(r, 9), d_ = triple_at(r, 12);
      e_ = triple_at(r, 15);
      finished_ = int32_t(r.body[18]);
    }
    // The resampler is bypassed for a 48 kHz wave whose pitch and rate are
    // the constants 0 and 1.
    bypass_ = wave_ && wave_->rate == 48000 && !a_.connected() && a_.value == 0.0f && !b_.connected() &&
              b_.value == 1.0f;
  }

  uint32_t flags() const override {
    const uint32_t ch = stereo_ ? 0x1040u : 0x0840u;
    return (one_shot_ ? 0x10010000u : 0x10210000u) | ch;
  }
  uint32_t event_inputs() const override { return 2; }
  int out_channels() const override { return stereo_ ? 2 : 1; }

  void init_state(Object&) override {
    voices_.clear();
    for (uint32_t i = 0; i < voices_n_; ++i) {
      voices_.emplace_back(Player(wave_, !one_shot_, bypass_), one_shot_);
      voices_.back().out.channels = out_channels();
    }
    cached_a_ = cached_b_ = cached_c_ = cached_d_ = -1.0f;
    stamp_ = UINT32_MAX;
    render_stamp_ = UINT32_MAX;
    last_ = -1;
    active_count_ = 0;
    playing_ = false;
    progress_ = 0.0f;
    mix_prev_ = 0.0f;
    mix_active_ = false;
  }

  // Update (sub_820F55F0, sub_820F1DF8, sub_820F2CD0, sub_820F08B8): once
  // per frame; when only the rate (gain) changes the pitch (dB) factor is
  // left out.
  void update(Object& o) override {
    if (stamp_ == o.frame()) return;
    const float b = o.read(b_);
    bool chg = b != cached_b_;
    if (chg) cached_b_ = b;
    float rate = b;
    const float a = o.read(a_);
    if (a != cached_a_) {
      cached_a_ = a;
      if (a != 0.0f) rate = float(std::pow(2.0, double(a))) * b;
      chg = true;
    }
    if (chg)
      for (Voice& v : voices_) v.rate = rate;
    float d = o.read(d_);
    if (type == 0x38 && d < 0.0f) d = 0.0f;
    chg = d != cached_d_;
    if (chg) cached_d_ = d;
    float gain = d;
    const float c = o.read(c_);
    if (c != cached_c_) {
      cached_c_ = c;
      if (c != 0.0f) gain = float(std::exp(double((kLn10 * c) * 0.05f))) * d;
      chg = true;
    }
    if (chg) {
      // The stereo one-shot sets voice 0 only.
      for (size_t i = 0; i < voices_.size(); ++i)
        if (type != 0x35 || i == 0) voices_[i].g34 = gain;
    }
    if (one_shot_) {
      if (last_ != -1) {
        const float p = voices_[size_t(last_)].player.progress();
        progress_ = p >= 1.0f ? 0.0f : p;
      }
    } else {
      progress_ = voices_[0].player.progress();
    }
    stamp_ = o.frame();
  }

  void receive_event(Object& o, uint32_t pin) override {
    if (pin == 0) {
      update(o);
      play(o);
    } else if (pin == 1) {
      for (Voice& v : voices_) v.stop();
      if (one_shot_) active_count_ = 0;
      else playing_ = false;
    }
  }

  float control(Object&, uint32_t pin) override {
    if (pin == 0) return progress_;
    if (pin == 1) return one_shot_ ? float(active_count_) : (playing_ ? 1.0f : 0.0f);
    return 0.0f;
  }

  bool render(Object& o) override {
    if (render_stamp_ == o.frame()) return mix_now_;
    render_stamp_ = o.frame();
    auto done = [&] { on_finished(o); };
    if (voices_.size() == 1) {
      const bool active = voices_[0].render(o, done);
      out = voices_[0].out;
      mix_now_ = active;
      return active;
    }
    // N voices summed by a Core::Mixer at gain 1 (sub_821041D8): mixed only
    // when it was active in the previous frame; its gains start from 0.
    bool any = false;
    std::vector<bool> act(voices_.size());
    for (size_t i = 0; i < voices_.size(); ++i) {
      act[i] = voices_[i].render(o, done);
      any = any || act[i];
    }
    out.clear();
    out.channels = out_channels();
    if (mix_active_) {
      for (size_t i = 0; i < voices_.size(); ++i) {
        if (!act[i]) continue;
        for (int c = 0; c < out.channels; ++c) {
          const float* in = voices_[i].out.ch[size_t(c)].data();
          float* s = out.ch[size_t(c)].data();
          if (mix_prev_ == 1.0f) {
            for (int n = 0; n < kFrameSamples; ++n) s[n] = in[n] + s[n];
          } else {
            const float d = 1.0f - mix_prev_;
            const float g0 = std::fma(o.ramp_start, d, mix_prev_);
            const float g1 = std::fma(o.ramp_span + o.ramp_start, d, mix_prev_);
            const float inc = (g1 - g0) * (1.0f / 256.0f);
            for (int n = 0; n < kFrameSamples; ++n) s[n] = std::fma(in[n], std::fma(float(n), inc, g0), s[n]);
          }
        }
      }
    }
    mix_active_ = any;
    mix_now_ = any;
    if (o.sub == kUpdatePeriod - 1) mix_prev_ = 1.0f;
    return any;
  }

 private:
  void play(Object& o) {
    if (one_shot_) {
      // sub_820F4C80: the next voice, restarted in place.
      last_ = last_ + 1 >= int32_t(voices_.size()) ? 0 : last_ + 1;
      Voice& v = voices_[size_t(last_)];
      if (!v.played_last) ++active_count_;
      v.play(0);
    } else {
      // sub_820F3090 / sub_820F0C00: from the fraction E of the wave.
      const float e = o.read(e_);
      Voice& v = voices_[0];
      if (!v.started) playing_ = true;
      const uint32_t len = v.player.length();
      v.play(uint32_t(int64_t(float(len) * e)));
    }
  }

  // The end of a wave, called synchronously from the render (sub_820E9788
  // -> sub_820F5B30 / sub_820F2310 / sub_820F31D8 / sub_820F0D40).
  void on_finished(Object& o) {
    o.start(finished_);
    if (one_shot_) --active_count_;
    else playing_ = false;
  }

  const Wave* wave_ = nullptr;
  bool one_shot_ = true, stereo_ = false, bypass_ = false;
  uint32_t voices_n_ = 1;
  Triple a_, b_, c_, d_, e_;
  int32_t finished_ = -1;
  std::vector<Voice> voices_;
  float cached_a_ = -1, cached_b_ = -1, cached_c_ = -1, cached_d_ = -1;
  uint32_t stamp_ = UINT32_MAX, render_stamp_ = UINT32_MAX;
  int32_t last_ = -1;
  int32_t active_count_ = 0;
  bool playing_ = false, mix_active_ = false, mix_now_ = false;
  float progress_ = 0.0f, mix_prev_ = 0.0f;
};

}  // namespace

std::unique_ptr<Component> make_source_component(const AobjRecord& r, const Aobj& def, const WaveStore* waves) {
  switch (r.type) {
    case 0x34: case 0x35: case 0x38: case 0x39: return std::make_unique<Source>(r, def, waves);
    default: return nullptr;
  }
}

}  // namespace geow2::audio

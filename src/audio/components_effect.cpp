// The effects attached to mixers. Each component's Update sets the
// parameters of a core unit; the mixer runs unit 0 of each attached effect
// in place on its output (sub_8210DF60). Coefficients follow the original's
// formulas, quirks included (atan in the one-pole filters, half the angle in
// the resonant low-pass, the linear amplitude where the biquads expect its
// square root).

#include <algorithm>
#include <cfloat>
#include <cmath>

#include "audio/components.h"

namespace geow2::audio {

namespace {

constexpr float kPi = 3.14159274f;       // dat_82018A50
constexpr float kTwoPi = 6.28318548f;    // dat_82018A58
constexpr float kInvRate = 2.08333331e-05f;  // dat_82197888
constexpr float kLn10 = 2.30258512f;     // dat_82018A6C
constexpr float kMinGain = 1.0e-4f;      // dat_82018A64

float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }

// fc = clamp(2^octaves * (mult * freq), 20, 24000).
float cutoff(float freq, float octaves, float mult) {
  return clampf(float(std::pow(2.0, double(octaves))) * (mult * freq), 20.0f, 24000.0f);
}

// 10^(dB/20) * gain, as exp((ln10 * dB) * 0.05) (sub_821308B8).
float db_gain(float db, float gain) { return float(std::exp(double((kLn10 * db) * 0.05f))) * gain; }

// The one-pole coefficient a = (1 - k) / (1 + k) with k = atan(...) (the
// original refines fres(1 + k) by two Newton steps).
float one_pole(float k) { return -((1.0f / (k + 1.0f)) * (k - 1.0f)); }

int unit_channels(uint32_t type) {
  switch (type) {
    case 0x7F: case 0x82: case 0x85: case 0xB2: return 1;
    default: return 2;
  }
}

class Effect : public Component {
 public:
  uint32_t flags() const override { return 0x10040000; }
  uint32_t event_inputs() const override { return 2; }

 protected:
  Effect(const AobjRecord& r, size_t first, int count) {
    for (int i = 0; i < count; ++i) t_.push_back(triple_at(r, first + 3 * size_t(i)));
    channels_ = unit_channels(r.type);
  }
  int channels(const AudioBuffer& b) const { return std::min(channels_, b.channels); }
  std::vector<Triple> t_;
  int channels_ = 1;
};

// 0x7F MonoLowPass [freq, octaves, mult, dB, gain] (Update sub_82111B18,
// unit type 3 sub_821296B0): the cutoff slews by at most half its value per
// frame; the gain is applied after the filter.
class OnePoleLowPass : public Effect {
 public:
  explicit OnePoleLowPass(const AobjRecord& r) : Effect(r, 1, 5) {}
  void init_state(Object&) override {
    c_[0] = 0.0f, c_[1] = 0.0f, c_[2] = 1.0f, c_[3] = 0.0f, c_[4] = 1.0f;
    target_ = cur_ = 24000.0f;
    gain_ = 1.0f;
    last_ = 0.0f;
    for (int c = 0; c < 6; ++c) x1_[c] = y1_[c] = 0.0f;
  }
  void update(Object& o) override {
    float v[5];
    for (int i = 0; i < 5; ++i) v[i] = o.read(t_[size_t(i)]);
    if (v[0] != c_[0] || v[2] != c_[2] || v[1] != c_[1]) {
      c_[0] = v[0], c_[1] = v[1], c_[2] = v[2];
      target_ = cutoff(v[0], v[1], v[2]);
    }
    if (v[4] != c_[4] || v[3] != c_[3]) {
      c_[3] = v[3], c_[4] = v[4];
      const float g = db_gain(v[3], v[4]);
      gain_ = g >= 0.0f ? g : 0.0f;
    }
  }
  void process_effect(Object&, AudioBuffer& b) override {
    const float m = (cur_ * 256.0f) * 0.001953125f;
    cur_ = clampf(target_ - cur_, -m, m) + cur_;
    if (cur_ != last_) {
      last_ = cur_;
      const float k = float(std::atan(double((cur_ * kInvRate) * kPi)));
      a_ = one_pole(k);
      b_ = (1.0f - a_) * 0.5f;
    }
    for (int c = 0; c < channels(b); ++c) {
      float* s = b.ch[size_t(c)].data();
      for (int n = 0; n < kFrameSamples; ++n) {
        const float x = s[n];
        const float y = std::fma(x + x1_[c], b_, a_ * y1_[c]);
        s[n] = gain_ * y;
        x1_[c] = x;
        y1_[c] = y;
      }
    }
  }

 private:
  float c_[5] = {};
  float target_ = 24000, cur_ = 24000, gain_ = 1, last_ = 0, a_ = 0, b_ = 0;
  float x1_[6] = {}, y1_[6] = {};
};

// 0x82 MonoHighPass / 0x83 StereoHighPass [freq, octaves, mult, dB, gain]
// (Updates sub_821131F0 / sub_821153D8, unit type 5 sub_82128E80). The
// original's output is y = a y[n-1] + b (x[n-1] - x[n]), the inverse of the
// textbook high-pass, evaluated four samples at a time.
class HighPass : public Effect {
 public:
  HighPass(const AobjRecord& r, bool stereo) : Effect(r, 1, 5), stereo_(stereo) {}
  void init_state(Object&) override {
    if (stereo_) c_[0] = 0.0f, c_[1] = 1.0f, c_[2] = 0.0f, c_[3] = 1.0f;
    else c_[0] = 1.0f, c_[1] = 0.0f, c_[2] = 1.0f, c_[3] = 0.0f, c_[4] = 1.0f;
    fc_ = 24000.0f;
    gain_ = 1.0f;
    last_ = -1.0f;
    for (auto& h : x_) h = {};
    for (auto& h : y_) h = {};
  }
  void update(Object& o) override {
    float v[5];
    for (int i = 0; i < 5; ++i) v[i] = o.read(t_[size_t(i)]);
    if (!stereo_) {
      if (v[0] != c_[0] || v[2] != c_[2] || v[1] != c_[1]) {
        c_[0] = v[0], c_[1] = v[1], c_[2] = v[2];
        fc_ = cutoff(v[0], v[1], v[2]);
      }
      if (v[4] != c_[4] || v[3] != c_[3]) {
        c_[3] = v[3], c_[4] = v[4];
        const float g = db_gain(v[3], v[4]);
        gain_ = g >= 0.0f ? g : 0.0f;
      }
      return;
    }
    // The stereo update's partial rules (kept): the frequency is never
    // cached, a zero multiplier or octave count is ignored, and whichever
    // of the two did not change is left out.
    float f = v[0];
    bool ch = false;
    if (v[2] != 0.0f && v[2] != c_[1]) {
      c_[1] = v[2];
      f = v[2] * f;
      ch = true;
    }
    if (v[1] != 0.0f && v[1] != c_[0]) {
      c_[0] = v[1];
      f = float(std::pow(2.0, double(v[1]))) * f;
      ch = true;
    }
    if (ch) fc_ = clampf(f, 20.0f, 24000.0f);
    float g = v[4];
    ch = v[4] != c_[3];
    c_[3] = v[4];
    if (v[3] != c_[2]) {
      c_[2] = v[3];
      g = db_gain(v[3], v[4]);
      ch = true;
    }
    if (ch) gain_ = g >= 0.0f ? g : 0.0f;
  }
  void process_effect(Object&, AudioBuffer& b) override {
    if (fc_ != last_) {
      last_ = fc_;
      const float k = float(std::atan(double((kInvRate * kPi) * fc_)));
      a_ = one_pole(k);
      b_ = (a_ + 1.0f) * 0.5f;
    }
    const float a2 = a_ * a_;
    const float a3 = a_ * a2;
    const float c0 = b_ * a3, c1 = b_ * a2, c2 = b_ * a_, c3 = b_;
    const float a4 = a2 * a2;
    for (int c = 0; c < channels(b); ++c) {
      float* s = b.ch[size_t(c)].data();
      auto& xh = x_[size_t(c)];  // x[n-4] .. x[n-1]
      auto& yh = y_[size_t(c)];  // y[n-4] .. y[n-1]
      for (int n = 0; n < kFrameSamples; ++n) {
        const float x = s[n];
        const float e0 = xh[2] - xh[3], e1 = xh[1] - xh[2], e2 = xh[3] - x, e3 = xh[0] - xh[1];
        // e[n-3], e[n-2], e[n-1], e[n] with e[j] = x[j-1] - x[j].
        const float dot = ((c0 * e3 + c1 * e1) + c2 * e0) + c3 * e2;
        const float y = std::fma(a4, yh[0], dot);
        for (int i = 0; i < 3; ++i) xh[size_t(i)] = xh[size_t(i) + 1], yh[size_t(i)] = yh[size_t(i) + 1];
        xh[3] = x;
        yh[3] = y;
        s[n] = y * gain_;
      }
    }
  }

 private:
  bool stereo_;
  float c_[5] = {};
  float fc_ = 24000, gain_ = 1, last_ = -1, a_ = 0, b_ = 0;
  std::array<std::array<float, 4>, 6> x_{}, y_{};
};

// 0x85 MonoResonantLowPass / 0x86 StereoResonantLowPass [freq, octaves,
// mult, resonance, dB, gain, unused] (Update sub_821135F0 / sub_82113AE8,
// unit type 4 sub_821291F8): a biquad at w = pi fc / Fs (half the textbook
// angle), Q = 2^(8 r), with the gain in both the coefficients and the output.
class ResonantLowPass : public Effect {
 public:
  explicit ResonantLowPass(const AobjRecord& r) : Effect(r, 1, 7) {}
  void init_state(Object&) override {
    for (float& c : c_) c = -1.0f;
    target_ = cur_ = 24000.0f;
    r_ = 0.0f;
    gain_ = 1.0f;
    last_fc_ = last_r_ = last_g_ = -1.0f;
    for (auto& s : st_) s = {};
  }
  void update(Object& o) override {
    float v[7];
    for (int i = 0; i < 7; ++i) v[i] = o.read(t_[size_t(i)]);
    if (v[0] != c_[0] || v[1] != c_[1] || v[2] != c_[2]) {
      c_[0] = v[0], c_[1] = v[1], c_[2] = v[2];
      target_ = cutoff(v[0], v[1], v[2]);
    }
    if (v[3] != c_[3]) {
      c_[3] = v[3];
      r_ = clampf(v[3], 0.0f, 1.0f);
    }
    if (v[5] != c_[5] || v[4] != c_[4]) {
      c_[4] = v[4], c_[5] = v[5];
      const float g = db_gain(v[4], v[5]);
      gain_ = g >= 0.0f ? g : 0.0f;
    }
    c_[6] = v[6];  // stored by the original and never used
  }
  void process_effect(Object&, AudioBuffer& b) override {
    // The slew limit uses the bare fres estimate in the original.
    const float m = (((1.0f / float(std::pow(2.0, double(r_ * 3.0f)))) * cur_) * 256.0f) * 0.001953125f;
    cur_ = cur_ + clampf(target_ - cur_, -m, m);
    if (cur_ != last_fc_ || r_ != last_r_ || gain_ != last_g_) {
      last_fc_ = cur_, last_r_ = r_, last_g_ = gain_;
      const double q = double(float(std::pow(2.0, double(r_ * 8.0f))));
      const float w = (cur_ * kInvRate) * kPi;
      const double s = double(float(std::sin(double(w))));
      const double c = double(float(std::cos(double(w))));
      const double alpha = s / (q * 2.0);
      const double inv = 1.0 / (alpha + 1.0);
      a1_ = float(c * -2.0 * inv);
      a2_ = float((1.0 - alpha) * inv);
      b1_ = float((1.0 - c) * inv) * gain_;
      b0_ = b1_ * 0.5f;
    }
    for (int ch = 0; ch < channels(b); ++ch) {
      float* s = b.ch[size_t(ch)].data();
      State& st = st_[size_t(ch)];
      for (int n = 0; n < kFrameSamples; ++n) {
        const float x = s[n];
        const float fir = (b0_ * st.x2 + b1_ * st.x1) + b0_ * x;
        const float y = std::fma(st.y2, -a2_, std::fma(st.y1, -a1_, fir));
        st.x2 = st.x1, st.x1 = x;
        st.y2 = st.y1, st.y1 = y;
        s[n] = y * gain_;
      }
    }
  }

 private:
  struct State {
    float x1, x2, y1, y2;
  };
  float c_[7] = {};
  float target_ = 24000, cur_ = 24000, r_ = 0, gain_ = 1, last_fc_ = -1, last_r_ = -1, last_g_ = -1;
  float a1_ = 0, a2_ = 0, b0_ = 0, b1_ = 0;
  std::array<State, 6> st_{};
};

// The peaking / shelving biquad loop of unit types 7 and 9 (sub_82128B68,
// sub_82128820), clipped to +-limit.
struct Biquad {
  float b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0, limit = FLT_MAX;
  struct State {
    float x1, x2, y1, y2;
  };
  std::array<State, 6> st{};

  void run(float* s, int ch, bool peak) {
    State& z = st[size_t(ch)];
    for (int n = 0; n < kFrameSamples; ++n) {
      const float x = s[n];
      // The peaking unit stores a1 in the place of b1 (they are equal).
      float t = (peak ? a1 : b1) * z.x1;
      t = std::fma(b0, x, t);
      t = std::fma(b2, z.x2, t);
      t = -std::fma(a1, z.y1, -t);
      float y = -std::fma(a2, z.y2, -t);
      y = std::min(std::max(y, -limit), limit);
      z.x2 = z.x1, z.x1 = x;
      z.y2 = z.y1, z.y1 = y;
      s[n] = y;
    }
  }
};

// 0x92 StereoPeakEQ [freq, octaves, mult, bandwidth, dB, gain, clip]
// (Update sub_821157D0, unit type 7): the RBJ peaking filter with the
// linear amplitude where RBJ uses its square root (the boost in dB is
// doubled), output clipped to +-min(clip, 1).
class PeakEq : public Effect {
 public:
  explicit PeakEq(const AobjRecord& r) : Effect(r, 1, 7) {}
  void init_state(Object&) override {
    for (float& c : c_) c = -1.0f;
    fc_ = 440.0f, bw_ = 0.0f, a_ = 1.0f;
    q_.limit = FLT_MAX;
    last_fc_ = last_bw_ = last_a_ = -1.0f;
    q_.st = {};
  }
  void update(Object& o) override {
    float v[7];
    for (int i = 0; i < 7; ++i) v[i] = o.read(t_[size_t(i)]);
    if (v[0] != c_[0] || v[1] != c_[1] || v[2] != c_[2]) {
      c_[0] = v[0], c_[1] = v[1], c_[2] = v[2];
      fc_ = cutoff(v[0], v[1], v[2]);
    }
    bw_ = clampf(v[3], 0.0f, 1.0f);
    if (v[5] != c_[5] || v[4] != c_[4]) {
      c_[4] = v[4], c_[5] = v[5];
      a_ = std::max(db_gain(v[4], v[5]), kMinGain);
    }
    q_.limit = std::min(v[6], 1.0f);
  }
  void process_effect(Object&, AudioBuffer& b) override {
    if (fc_ != last_fc_ || bw_ != last_bw_ || a_ != last_a_) {
      last_fc_ = fc_, last_bw_ = bw_, last_a_ = a_;
      const float q = float(std::pow(2.0, double(bw_ * 8.0f)));
      const float w = (kInvRate * kTwoPi) * fc_;
      const float s = float(std::sin(double(w))), c = float(std::cos(double(w)));
      const float alpha = s / (q * 2.0f);
      const float p = alpha / a_, qq = a_ * alpha;
      const float inv = 1.0f / (p + 1.0f);
      q_.a2 = (1.0f - p) * inv;
      q_.a1 = (c * -2.0f) * inv;
      q_.b1 = q_.a1;
      q_.b0 = (qq + 1.0f) * inv;
      q_.b2 = (1.0f - qq) * inv;
    }
    for (int c = 0; c < channels(b); ++c) q_.run(b.ch[size_t(c)].data(), c, true);
  }

 private:
  float c_[7] = {};
  float fc_ = 440, bw_ = 0, a_ = 1, last_fc_ = -1, last_bw_ = -1, last_a_ = -1;
  Biquad q_;
};

// 0xB2 MonoDistortion [pre dB, pre gain, shelf freq, shelf slope, post dB,
// post gain, drive dB, knee, output dB, output gain] (Update sub_821106D0):
// a low shelf, a waveshaper and a second shelf, of which mixers attach only
// unit 0 - so only the first low shelf (unit type 9, sub_82128820) is heard.
class Distortion : public Effect {
 public:
  explicit Distortion(const AobjRecord& r) : Effect(r, 1, 10) {}
  void init_state(Object&) override {
    for (float& c : c_) c = -1.0f;
    fc_ = 0.0f, s_ = 0.0f, a_ = 1.0f;
    last_fc_ = last_s_ = last_a_ = -1.0f;
    q_.st = {};
    q_.limit = FLT_MAX;  // the distortion never sets the shelf's clip
  }
  void update(Object& o) override {
    float v[10];
    for (int i = 0; i < 10; ++i) v[i] = o.read(t_[size_t(i)]);
    // The pre gain: when only the gain changes the dB factor is dropped.
    float g = std::max(v[1], 0.0f);
    bool ch = v[1] != c_[1];
    c_[1] = v[1];
    if (v[0] != c_[0] && v[0] != 0.0f) {
      c_[0] = v[0];
      g = db_gain(v[0], g);
      ch = true;
    }
    if (ch) a_ = std::max(g, kMinGain);
    fc_ = clampf(v[2], 20.0f, 24000.0f);
    s_ = clampf(v[3], kMinGain, 1.0f);
    // Triples 4..9 configure the units that never run.
  }
  void process_effect(Object&, AudioBuffer& b) override {
    if (fc_ != last_fc_ || s_ != last_s_ || a_ != last_a_) {
      last_fc_ = fc_, last_s_ = s_, last_a_ = a_;
      const double w = double(float((double(fc_) * double(kInvRate)) * double(kTwoPi)));
      const double s = double(float(std::sin(w))), c = double(float(std::cos(w)));
      const double sq = double(std::sqrt(a_));
      const double a = double(a_);
      const double beta = (sq * s) / double(s_);
      const double p = a + 1.0, m = a - 1.0;
      const double a0 = p + m * c + beta;
      q_.a1 = float(-2.0 * (m + p * c) / a0);
      q_.a2 = float((p + m * c - beta) / a0);
      q_.b0 = float(a * (p - m * c + beta) / a0);
      q_.b1 = float(2.0 * a * (m - p * c) / a0);
      q_.b2 = float(a * (p - m * c - beta) / a0);
    }
    for (int c = 0; c < channels(b); ++c) q_.run(b.ch[size_t(c)].data(), c, false);
  }

 private:
  float c_[10] = {};
  float fc_ = 0, s_ = 0, a_ = 1, last_fc_ = -1, last_s_ = -1, last_a_ = -1;
  Biquad q_;
};

// 0x77 StereoFeedbackDelay [max delay, delay, feedback, wet, dry] (Update
// sub_82119B68, unit type 0x12 sub_82128180): a delay of whole 256-sample
// frames; the feedback scales both the input and the recirculation, and a
// feedback of 0 leaves the line unwritten.
class FeedbackDelay : public Effect {
 public:
  explicit FeedbackDelay(const AobjRecord& r) : Effect(r, 2, 4) {
    const float max_delay = std::max(word_float(r.body[1]), 0.0f);
    frames_ = uint32_t(int64_t((max_delay * 0.00390625f) * 48000.0f));
  }
  void init_state(Object&) override {
    delay_ = UINT32_MAX;
    last_delay_ = UINT32_MAX - 1;
    pos_ = 0;
    fb_ = 0.0f, wet_ = 1.0f, dry_ = 1.0f;
    cached_delay_ = 0.0f;
    line_.assign(size_t(frames_ + 1) * size_t(channels_) * kFrameSamples, 0.0f);
  }
  void update(Object& o) override {
    const float d = o.read(t_[0]);
    if (d != cached_delay_) {
      cached_delay_ = d;
      const uint32_t f = uint32_t(int64_t(187.5f * d));
      delay_ = frames_ > 0 ? std::min(f, frames_ - 1) : 0;
    }
    fb_ = clampf(o.read(t_[1]), -1.0f, 1.0f);
    wet_ = std::max(o.read(t_[2]), 0.0f);
    dry_ = std::max(o.read(t_[3]), 0.0f);
  }
  void process_effect(Object&, AudioBuffer& b) override {
    if (frames_ == 0 || delay_ >= frames_) return;
    if (delay_ != last_delay_) {
      std::fill(line_.begin(), line_.end(), 0.0f);
      last_delay_ = delay_;
    }
    uint32_t w = delay_ + pos_;
    if (w >= frames_) w -= frames_;
    for (int c = 0; c < channels(b); ++c) {
      float* in = b.ch[size_t(c)].data();
      if (delay_ == 0) {
        if (dry_ != 1.0f)
          for (int n = 0; n < kFrameSamples; ++n) in[n] = in[n] * dry_;
        continue;
      }
      float* sw = slot(c, w);
      const float* sp = slot(c, pos_);
      for (int n = 0; n < kFrameSamples; ++n) {
        if (fb_ == 1.0f) sw[n] = in[n] + sp[n];
        else if (fb_ != 0.0f) sw[n] = std::fma(fb_, sp[n], in[n] * fb_);
        float x = dry_ == 0.0f ? 0.0f : (dry_ != 1.0f ? in[n] * dry_ : in[n]);
        x = wet_ == 1.0f ? x + sp[n] : std::fma(wet_, sp[n], x);
        in[n] = x;
      }
    }
    if (++pos_ >= frames_) pos_ = 0;
  }

 private:
  float* slot(int c, uint32_t i) { return line_.data() + (size_t(c) * (frames_ + 1) + i) * kFrameSamples; }
  uint32_t frames_ = 0, delay_ = UINT32_MAX, last_delay_ = 0, pos_ = 0;
  float fb_ = 0, wet_ = 1, dry_ = 1, cached_delay_ = 0;
  std::vector<float> line_;
};

}  // namespace

std::unique_ptr<Component> make_effect_component(const AobjRecord& r, const Aobj&) {
  switch (r.type) {
    case 0x7F: return std::make_unique<OnePoleLowPass>(r);
    case 0x82: return std::make_unique<HighPass>(r, false);
    case 0x83: return std::make_unique<HighPass>(r, true);
    case 0x85: case 0x86: return std::make_unique<ResonantLowPass>(r);
    case 0x92: return std::make_unique<PeakEq>(r);
    case 0xB2: return std::make_unique<Distortion>(r);
    case 0x77: return std::make_unique<FeedbackDelay>(r);
    default: return nullptr;
  }
}

}  // namespace geow2::audio

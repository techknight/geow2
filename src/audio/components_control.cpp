// The control components: parameters, actions, variables, scripts, timers,
// curves and the other control math. Each computes in single precision with
// the original's operation order; fused multiply-adds are written with
// std::fma where the original uses fmadds / fnmsubs.

#include <cfloat>
#include <cmath>
#include <cstdio>

#include "audio/components.h"

namespace geow2::audio {

namespace {

// PowerPC fsel: a >= 0 ? b : c (NaN gives c).
float fsel(float a, float b, float c) { return a >= 0.0f ? b : c; }

constexpr float kPi = 3.14159274f;           // dat_82018A50
constexpr float kInv2p32 = 2.32830644e-10f;  // dat_820006E4, 2^-32
constexpr float kEqualEpsilon = 1.0e-4f;     // dat_82018A64

int32_t word_index(uint32_t w) { return int32_t(w); }

// ---------------------------------------------------------------- interface

// 0x00 [name, default]: set by the game (message type 1, sub_820FBDF0).
class ParameterIn : public Component {
 public:
  explicit ParameterIn(const AobjRecord& r) : initial_(word_float(r.body[1])) {}
  uint32_t flags() const override { return 0x41; }
  void init_state(Object&) override { value = initial_; }  // sub_820FBCF8
  float control(Object&, uint32_t pin) override { return pin == 0 ? value : 0.0f; }  // sub_820FBD20
  float value = 0.0f;

 private:
  float initial_;
};

// 0x01 [name, value triple]: the game reads it back (mgr+0x74, under a lock).
class ParameterOut : public Component {
 public:
  explicit ParameterOut(const AobjRecord& r) : in_(triple_at(r, 1)), initial_(word_float(r.body[3])) {}
  uint32_t flags() const override { return 0x10000002; }
  void init_state(Object&) override { value = initial_; }  // sub_820FBF18
  void update(Object& o) override { value = o.read(in_); }  // sub_820FBFD8
  float value = 0.0f;

 private:
  Triple in_;
  float initial_;
};

// 0x02 [name, target]: fired by the game (message type 3, sub_820FC1D8).
class ActionIn : public Component {
 public:
  explicit ActionIn(const AobjRecord& r) : target(word_index(r.body[1])) {}
  uint32_t flags() const override { return 0x400004; }
  int32_t target;
};

// 0x12 [name, initial]: the only type a Script stores into (flags 0x240).
class Variable : public Component {
 public:
  explicit Variable(const AobjRecord& r) : initial_(word_float(r.body[1])) {}
  uint32_t flags() const override { return 0x240; }
  void init_state(Object&) override { value_ = initial_; }       // sub_82125978
  void set_value(Object&, float v) override { value_ = v; }      // sub_821259F8
  float control(Object&, uint32_t pin) override { return pin == 0 ? value_ : 0.0f; }  // sub_821259A0

 private:
  float initial_, value_ = 0.0f;
};

// 0xAB [name, suffix, input pair]: a developer log line on event 0; no
// effect on the sound.
class DebugPrint : public Component {
 public:
  explicit DebugPrint(const AobjRecord& r) : in_{int32_t(r.body[2]), r.body[3], 0.0f} {}
  uint32_t event_inputs() const override { return 1; }
  // Event 0 (sub_82125A48) reads its input (which stamps a GateSwitch) and
  // logs it.
  void receive_event(Object& o, uint32_t pin) override {
    if (pin == 0) (void)o.read(in_);
  }

 private:
  Triple in_;
};

// ------------------------------------------------------------------ scripts

// 0xA9 [name, unused, length, code...]: a small stack machine
// (sub_82120A28), run to completion within one control update.
class Script : public Component {
 public:
  explicit Script(const AobjRecord& r) {
    const uint32_t n = r.body[2];
    code_.assign(r.body.begin() + 3, r.body.begin() + 3 + n);
  }
  uint32_t flags() const override { return 0x90000080; }
  uint32_t event_inputs() const override { return 1; }
  void init_state(Object&) override { reset(); running_ = false; }  // sub_82120978
  void start(Object&) override { running_ = true; }                  // sub_821209D0
  void receive_event(Object& o, uint32_t pin) override {             // sub_821209A8
    if (pin == 0) start(o);
  }
  void update(Object& o) override {  // sub_82121518
    // The original loops until END; the cap only guards malformed code.
    for (int steps = 0; running_ && steps < (1 << 20); ++steps) step(o);
    running_ = false;
  }

 private:
  void reset() {
    pc_ = 0;
    sp_ = 0;
    eq_ = gt_ = lt_ = false;
  }
  float& top() { return stack_[(sp_ - 1) & 15]; }
  uint32_t operand() { return pc_ < code_.size() ? code_[pc_++] : 0; }
  void jump(bool taken) {
    if (taken && pc_ < code_.size()) pc_ = code_[pc_];
    else ++pc_;
  }
  void push(float v) {
    if (sp_ < 16) stack_[sp_] = v;
    ++sp_;
  }

  void step(Object& o) {
    if (pc_ >= code_.size()) {
      running_ = false;
      reset();
      return;
    }
    const uint32_t op = code_[pc_++];
    switch (op) {
      case 0x1000: {  // EVENT c, p: ReceiveEvent(p) on c, synchronously
        const uint32_t c = operand(), p = operand();
        if (c < o.comps.size() && p < o.comps[c]->event_inputs()) o.event(int32_t(c), p);
        break;
      }
      case 0x1001:  // PUSH GetControl(c, 0)
        push(o.control(operand(), 0));
        break;
      case 0x1002: {  // POP into Variable c
        const uint32_t c = operand();
        --sp_;
        if (c < o.comps.size() && (o.comps[c]->flags() & kFlagSettable))
          o.comps[c]->set_value(o, stack_[sp_ & 15]);
        break;
      }
      case 0x1003:  // POP
        --sp_;
        break;
      case 0x1004:  // PUSH constant
        push(word_float(operand()));
        break;
      case 0x1100: jump(true); break;
      case 0x1101: jump(eq_); break;
      case 0x1102: jump(!eq_); break;
      case 0x1103: jump(lt_); break;
      case 0x1104: jump(!lt_); break;
      case 0x1105: jump(gt_); break;
      case 0x1106: jump(!gt_); break;
      case 0x1200:  // START c
        o.start(int32_t(operand()));
        break;
      case 0x1202:
      case 0x1203:  // END
        running_ = false;
        reset();
        break;
      case 0x1300: {  // COMPARE stack[sp-2] with stack[sp-1], no pop
        const float a = stack_[(sp_ - 2) & 15], b = stack_[(sp_ - 1) & 15];
        eq_ = std::fabs(a - b) < kEqualEpsilon;
        lt_ = a < b;
        gt_ = b < a;
        break;
      }
      case 0x1301: { const uint32_t c = operand(); top() = o.control(c, 0) + top(); break; }
      case 0x1302: { const uint32_t c = operand(); top() = top() - o.control(c, 0); break; }
      case 0x1303: { const uint32_t c = operand(); top() = o.control(c, 0) * top(); break; }
      case 0x1304: {
        // The divisor is read twice: once for the original's assert, once
        // for the division.
        const uint32_t c = pc_ < code_.size() ? code_[pc_] : 0;
        (void)o.control(c, 0);
        ++pc_;
        top() = top() / o.control(c, 0);
        break;
      }
      case 0x1305: top() = word_float(operand()) + top(); break;
      case 0x1306: top() = top() - word_float(operand()); break;
      case 0x1307: top() = word_float(operand()) * top(); break;
      case 0x1308: top() = top() / word_float(operand()); break;
      case 0x0000:  // asserts, then restarts from the top
        reset();
        break;
      default:  // asserts; the opcode is skipped
        break;
    }
  }

  std::vector<uint32_t> code_;
  float stack_[16] = {};
  uint32_t pc_ = 0, sp_ = 0;
  bool running_ = false, eq_ = false, gt_ = false, lt_ = false;
};

// ------------------------------------------------------------------- timing

// 0x1A [name, start, rate, alarm time, alarm target].
class Timer : public Component {
 public:
  explicit Timer(const AobjRecord& r)
      : start_(triple_at(r, 1)), rate_(triple_at(r, 4)), alarm_(triple_at(r, 7)), target_(word_index(r.body[10])) {}
  uint32_t flags() const override { return 0x10000040; }
  uint32_t event_inputs() const override { return 4; }
  void init_state(Object& o) override {  // sub_82120610
    running_ = false;
    armed_ = true;
    time_ = o.read(start_);
  }
  void receive_event(Object& o, uint32_t pin) override {  // sub_821207F0
    switch (pin) {
      case 0: running_ = true; break;
      case 1: running_ = false; break;
      case 2: time_ = o.read(start_); armed_ = true; break;  // sub_82120880
      case 3: time_ = 0.0f; armed_ = true; break;
      default: break;
    }
  }
  void update(Object& o) override {  // sub_821206B8
    if (!running_) return;
    const float rate = o.read(rate_);
    time_ = std::fma(o.dt(), rate, time_);
    if (armed_ && target_ != -1) {
      const float alarm = o.read(alarm_);
      if (!(time_ < alarm)) {
        armed_ = false;
        o.start(target_);
      }
    }
  }
  float control(Object&, uint32_t pin) override {  // sub_82120568
    if (pin == 0) return time_;
    return pin == 1 ? (running_ ? 1.0f : 0.0f) : 0.0f;
  }

 private:
  Triple start_, rate_, alarm_;
  int32_t target_;
  bool running_ = false, armed_ = true;
  float time_ = 0.0f;
};

// 0x2E [name, target pair, attack, hold, release, attack/hold/release end
// targets]: rises at 1/attack per second to the target, holds, falls at
// 1/release per second (sub_82123680).
class Envelope : public Component {
 public:
  explicit Envelope(const AobjRecord& r)
      : in_(pair_at(r, 1)),
        attack_(triple_at(r, 3)),
        hold_(triple_at(r, 6)),
        release_(triple_at(r, 9)),
        attack_end_(word_index(r.body[12])),
        hold_end_(word_index(r.body[13])),
        release_end_(word_index(r.body[14])) {}
  uint32_t flags() const override { return 0x10000040; }
  void init_state(Object&) override { value_ = hold_left_ = 0.0f; }  // sub_82123608
  float control(Object&, uint32_t pin) override { return pin == 0 ? value_ : 0.0f; }  // sub_82123628
  void update(Object& o) override {
    const float dt = o.dt();
    const float x = o.read(in_);
    if (!(value_ >= x)) {
      // max(attack, 1e-4) by fsel; the original's reciprocal is fres (an
      // estimate), here an exact division.
      float a = o.read(attack_);
      a = fsel(a - kEqualEpsilon, a, kEqualEpsilon);
      value_ = std::fma(1.0f / a, dt, value_);
      if (!(value_ < x)) {
        value_ = x;
        if (attack_end_ != -1) o.start(attack_end_);
        hold_left_ = o.read(hold_);
      }
    } else if (value_ > x) {
      if (hold_left_ > 0.0f) {
        if (hold_.connected()) (void)o.read(hold_);  // read and discarded, as the original
        hold_left_ = hold_left_ - dt;
        if (!(hold_left_ > 0.0f)) {
          hold_left_ = 0.0f;
          if (hold_end_ != -1) o.start(hold_end_);
        }
      } else {
        float r = o.read(release_);
        r = fsel(r - kEqualEpsilon, r, kEqualEpsilon);
        value_ = std::fma(-(1.0f / r), dt, value_);
        if (!(value_ >= x)) {
          value_ = x;
          if (release_end_ != -1) o.start(release_end_);
        }
      }
    }
  }

 private:
  Triple in_, attack_, hold_, release_;
  int32_t attack_end_, hold_end_, release_end_;
  float value_ = 0.0f, hold_left_ = 0.0f;
};

// 0x30 [name, input pair, threshold, hysteresis, low, high, rising target,
// falling target] (sub_8211D9B0, sub_8211DBE0).
class GateSwitch : public Component {
 public:
  explicit GateSwitch(const AobjRecord& r)
      : in_(pair_at(r, 1)),
        threshold_(triple_at(r, 3)),
        hysteresis_(triple_at(r, 6)),
        low_(triple_at(r, 9)),
        high_(triple_at(r, 12)),
        rising_(word_index(r.body[15])),
        falling_(word_index(r.body[16])) {}
  uint32_t flags() const override { return 0x10000040; }
  void init_state(Object&) override {  // sub_8211D988
    high_state_ = false;
    x_ = FLT_MAX;
    // The original leaves the stamp as the allocator left it; here it starts
    // matching no frame.
    stamp_ = UINT32_MAX;
  }
  void update(Object& o) override {
    const float x = o.read(in_);
    const float thr = o.read(threshold_);
    float hyst = o.read(hysteresis_);
    if (hyst < 0.0f) hyst = hyst * -1.0f;  // dat_82000C88
    // NaN fires the edges (the original tests !(x <= thr + hyst) and
    // !(thr - hyst <= x)).
    if (!high_state_) {
      if (!(x <= hyst + thr)) {
        high_state_ = true;
        if (rising_ != -1) o.start(rising_);
      }
    } else if (!(thr - hyst <= x)) {
      high_state_ = false;
      if (falling_ != -1) o.start(falling_);
    }
    x_ = x;
  }
  float control(Object& o, uint32_t pin) override {
    if (pin != 0) return 0.0f;
    // Quirk kept: a second read in the same frame returns the stored input,
    // not the output.
    if (stamp_ == o.frame()) return x_;
    stamp_ = o.frame();
    return o.read(high_state_ ? high_ : low_);
  }

 private:
  Triple in_, threshold_, hysteresis_, low_, high_;
  int32_t rising_, falling_;
  bool high_state_ = false;
  float x_ = FLT_MAX;
  uint32_t stamp_ = UINT32_MAX;
};

// ------------------------------------------------------------- control math

// 0x13 [name, x pair, shape, N, N x {x, y triple}]: a linear scan from the
// first point, so unsorted x values behave as the original (sub_8211CD38).
class PiecewiseLinear : public Component {
 public:
  explicit PiecewiseLinear(const AobjRecord& r) : x_in_(pair_at(r, 1)), shape_(triple_at(r, 3)) {
    const uint32_t n = r.body[6];
    for (uint32_t i = 0; i < n; ++i) {
      const size_t k = 7 + 4 * size_t(i);
      points_.push_back({word_float(r.body[k]), {word_index(r.body[k + 2]), r.body[k + 3], word_float(r.body[k + 1])}});
    }
  }
  uint32_t flags() const override { return 0x40; }
  void init_state(Object&) override {  // sub_8211CCB0
    dirty_ = false;
    for (const Point& p : points_)
      if (p.y.connected()) dirty_ = true;
    stamp_ = UINT32_MAX;
    last_x_ = FLT_MAX;
    value_ = FLT_MAX;
  }
  float control(Object& o, uint32_t pin) override {
    if (pin != 0) return 0.0f;
    if (stamp_ == o.frame()) return value_;
    stamp_ = o.frame();
    const float x = o.read(x_in_);
    if (last_x_ == x && !dirty_ && !shape_.connected()) return value_;
    const float g = o.read(shape_);
    last_x_ = x;
    const size_t n = points_.size();
    if (n == 0) return value_;
    if (!(points_[0].x < x)) {
      value_ = o.read(points_[0].y);
    } else if (!(x < points_[n - 1].x)) {
      value_ = o.read(points_[n - 1].y);
    } else {
      size_t i = 1;
      while (points_[i].x < x) ++i;
      const float y0 = o.read(points_[i - 1].y);
      if (x == points_[i - 1].x) {
        value_ = y0;
        return value_;
      }
      float t = (x - points_[i - 1].x) / (points_[i].x - points_[i - 1].x);
      float u = 1.0f - t;
      if (g != 1.0f) {  // sub_821303B8 pow
        u = float(std::pow(double(u), double(g)));
        t = float(std::pow(double(t), double(g)));
      }
      const float y1 = o.read(points_[i].y);
      value_ = std::fma(y1, t, u * y0);
    }
    return value_;
  }

 private:
  struct Point {
    float x;
    Triple y;
  };
  Triple x_in_, shape_;
  std::vector<Point> points_;
  bool dirty_ = false;
  uint32_t stamp_ = UINT32_MAX;
  float last_x_ = FLT_MAX, value_ = FLT_MAX;
};

// 0x31 [name, index pair, offset, N, N triples] (sub_8211D620).
class Array : public Component {
 public:
  explicit Array(const AobjRecord& r) : index_(pair_at(r, 1)), offset_(triple_at(r, 3)) {
    const uint32_t n = r.body[6];
    // One spare element past the end: the original's blend for an index in
    // (N-1, N) reads past its table; here that reads a constant 0.
    for (uint32_t i = 0; i <= n; ++i) e_.push_back(i < n ? triple_at(r, 7 + 3 * size_t(i)) : Triple{});
  }
  uint32_t flags() const override { return 0x40; }
  void init_state(Object&) override {  // sub_8211D598
    dirty_ = false;
    for (size_t i = 0; i + 1 < e_.size(); ++i)
      if (e_[i].connected()) dirty_ = true;
    stamp_ = UINT32_MAX;
    last_x_ = FLT_MAX;
    value_ = FLT_MAX;
  }
  float control(Object& o, uint32_t pin) override {
    if (pin != 0) return 0.0f;
    if (stamp_ == o.frame()) return value_;
    stamp_ = o.frame();
    const float x = o.read(index_);
    if (last_x_ == x && !dirty_) return value_;
    const float off = o.read(offset_);
    const float i = x - off;
    last_x_ = x;
    const uint32_t n = uint32_t(e_.size() - 1);
    if (!(0.0f <= i)) {
      value_ = o.read(e_[0]);
    } else if (double(n) <= double(i)) {
      value_ = o.read(e_[n - 1]);
    } else {
      const int k = int(i);
      const float frac = i - float(k);
      if (frac != 0.0f) {
        const float a = o.read(e_[size_t(k)]);
        const float b = o.read(e_[size_t(k) + 1]);
        value_ = std::fma(1.0f - frac, a, b * frac);
      } else {
        value_ = o.read(e_[size_t(k)]);
      }
    }
    return value_;
  }

 private:
  Triple index_, offset_;
  std::vector<Triple> e_;
  bool dirty_ = false;
  uint32_t stamp_ = UINT32_MAX;
  float last_x_ = FLT_MAX, value_ = FLT_MAX;
};

// 0x1F Sum, 0x20 Product, 0x24 Max, 0x25 Min [name, n, n triples]: a left
// fold, cached per frame (sub_82122608, sub_821223B0, sub_82122AF8,
// sub_82122860).
class Fold : public Component {
 public:
  explicit Fold(const AobjRecord& r) {
    type = r.type;
    const uint32_t n = r.body[1];
    for (uint32_t i = 0; i < n; ++i) in_.push_back(triple_at(r, 2 + 3 * size_t(i)));
  }
  uint32_t flags() const override { return 0x40; }
  void init_state(Object&) override {
    value_ = -1.0f;
    stamp_ = UINT32_MAX;
  }
  float control(Object& o, uint32_t pin) override {
    if (pin != 0) return 0.0f;
    if (stamp_ == o.frame()) return value_;
    stamp_ = o.frame();
    value_ = in_.empty() ? 0.0f : o.read(in_[0]);
    for (size_t i = 1; i < in_.size(); ++i) {
      const float t = o.read(in_[i]);
      switch (type) {
        case 0x1F: value_ = t + value_; break;
        case 0x20: value_ = t * value_; break;
        case 0x24: value_ = fsel(value_ - t, value_, t); break;
        default: value_ = fsel(t - value_, value_, t); break;
      }
    }
    return value_;
  }

 private:
  std::vector<Triple> in_;
  float value_ = -1.0f;
  uint32_t stamp_ = UINT32_MAX;
};

// 0x18 [name, x pair, cutoff]: a one-pole smoother, updated each control
// update (sub_8211D340).
class ControlLowPass : public Component {
 public:
  explicit ControlLowPass(const AobjRecord& r) : in_(pair_at(r, 1)), fc_(triple_at(r, 3)) {}
  uint32_t flags() const override { return 0x10000040; }
  void init_state(Object&) override {  // sub_8211D2B8
    xprev_ = y_ = 0.0f;
    cached_fc_ = -1.0f;
  }
  float control(Object&, uint32_t pin) override { return pin == 0 ? y_ : 0.0f; }  // sub_8211D2E8
  void update(Object& o) override {
    const float fc = o.read(fc_);
    if (fc != cached_fc_) {
      // atan (sub_8212FD88) where a bilinear design would use tan; kept.
      const float k = float(std::atan(double(o.dt() * kPi * fc)));
      cached_fc_ = fc;
      a1_ = -((k - 1.0f) / (k + 1.0f));
      a0_ = (1.0f - a1_) * 0.5f;
    }
    const float x = o.read(in_);
    const float prev = xprev_;
    xprev_ = x;
    y_ = std::fma(prev + x, a0_, a1_ * y_);
  }

 private:
  Triple in_, fc_;
  float a0_ = 0.0f, a1_ = 0.0f, xprev_ = 0.0f, y_ = 0.0f, cached_fc_ = -1.0f;
};

// 0x26 [name, x pair]: the rate of change per second, except that it
// multiplies by the update period instead of dividing by it (quirk kept:
// out = (x - prev) * N * 48000 / 256), sub_82123268.
class Gradient : public Component {
 public:
  explicit Gradient(const AobjRecord& r) : in_(pair_at(r, 1)) {}
  uint32_t flags() const override { return 0x10000040; }
  void init_state(Object&) override { prev_ = out_ = 0.0f; }  // sub_821231F0
  float control(Object&, uint32_t pin) override { return pin == 0 ? out_ : 0.0f; }  // sub_82123210
  void update(Object& o) override {
    const float x = o.read(in_);
    out_ = (((x - prev_) * float(o.n)) * 48000.0f) * 0.00390625f;
    prev_ = x;
  }

 private:
  Triple in_;
  float prev_ = 0.0f, out_ = 0.0f;
};

// 0x29 [name, x pair]: sin(pi x), with the original's range reduction
// (sub_82122F18): outside [-1, 1] it subtracts trunc(x) - 1 (x < -1) or
// trunc(x) + 1 (x > 1), which flips the sign when that is odd.
class Sin : public Component {
 public:
  explicit Sin(const AobjRecord& r) : in_(pair_at(r, 1)) {}
  uint32_t flags() const override { return 0x40; }
  void init_state(Object&) override {  // sub_82122EF0
    stamp_ = UINT32_MAX;
    value_ = 0.0f;
  }
  float control(Object& o, uint32_t pin) override {
    if (pin != 0) return 0.0f;
    if (stamp_ == o.frame()) return value_;
    stamp_ = o.frame();
    float x = o.read(in_);
    if (x < -1.0f) {
      const int k = int(x) - 1;
      x = std::fma(-(float(k) * 0.5f), 2.0f, x);
    } else if (x > 1.0f) {
      const int k = int(x) + 1;
      x = std::fma(-(float(k) * 0.5f), 2.0f, x);
    }
    value_ = float(std::sin(double(kPi * x)));  // sub_8212E9B0
    return value_;
  }

 private:
  Triple in_;
  uint32_t stamp_ = UINT32_MAX;
  float value_ = 0.0f;
};

// 0x1D [name, min, max]: event 0 uniform, 1 integer in range, 2 an integer
// different from the current one.
class Random : public Component {
 public:
  explicit Random(const AobjRecord& r) : min_(triple_at(r, 1)), max_(triple_at(r, 4)) {}
  uint32_t flags() const override { return 0x40; }
  uint32_t event_inputs() const override { return 3; }
  void init_state(Object&) override { value_ = 0.0f; }  // sub_82121668
  float control(Object&, uint32_t pin) override { return pin == 0 ? value_ : 0.0f; }  // sub_82121680
  void receive_event(Object& o, uint32_t pin) override {
    if (pin == 0) {  // sub_821216D8: min + (max - min) * u
      const float lo = o.read(min_), hi = o.read(max_);
      const float s = float(o.shared.next_random());
      value_ = std::fma((hi - lo) * s, kInv2p32, lo);
    } else if (pin == 1) {  // sub_82121820: trunc(min + 0.9999) .. trunc(max)
      const float lo_f = o.read(min_), hi_f = o.read(max_);
      const int lo = int((lo_f + 1.0f) - kEqualEpsilon);
      const int n = int(hi_f) - lo + 1;
      const float s = float(o.shared.next_random());
      value_ = float(lo - int((float(n) * s) * -kInv2p32));
    } else if (pin == 2) {  // sub_821219E0
      const float lo = o.read(min_);
      if (!(value_ >= lo)) value_ = lo;
      const float hi = o.read(max_);
      const int d = int(hi - lo);
      const int n = d + 1;
      const float s = float(o.shared.next_random());
      const int k = int((float(d) * s) * -kInv2p32);
      const int v = int(float(1 - k) + (value_ - lo));
      value_ = float(n != 0 ? v % n : v) + lo;
    }
  }

 private:
  Triple min_, max_;
  float value_ = 0.0f;
};

// 0xAA [name, N, minimum total, N x {value, weight, multiplier,
// increment}]: weighted random choice (sub_82121E80); event 1 resets the
// weights (sub_82121D30).
class Selector : public Component {
 public:
  explicit Selector(const AobjRecord& r) : min_total_(triple_at(r, 2)) {
    const uint32_t n = r.body[1];
    for (uint32_t i = 0; i < n; ++i) {
      const size_t k = 5 + 12 * size_t(i);
      entries_.push_back({triple_at(r, k), triple_at(r, k + 3), triple_at(r, k + 6), triple_at(r, k + 9)});
    }
    w_.resize(n);
  }
  uint32_t flags() const override { return 0x40; }
  uint32_t event_inputs() const override { return 2; }
  void init_state(Object& o) override {  // sub_82121E00
    out_ = 0.0f;
    reset(o);
  }
  float control(Object&, uint32_t pin) override { return pin == 0 ? out_ : 0.0f; }  // sub_82121CD8
  void receive_event(Object& o, uint32_t pin) override {  // sub_821222C0
    if (pin == 0) pick(o);
    else if (pin == 1) reset(o);
  }

 private:
  struct Entry {
    Triple value, weight, mult, inc;
  };
  void reset(Object& o) {
    for (size_t i = 0; i < entries_.size(); ++i) w_[i] = o.read(entries_[i].weight);
  }
  void pick(Object& o) {
    const size_t n = entries_.size();
    if (n == 0) return;
    float sum = 0.0f;
    for (float w : w_) sum = w + sum;
    float t = o.read(min_total_);
    if (!(t >= 0.0f)) t = 0.0f;
    if (!(sum > t)) {
      reset(o);
      sum = 0.0f;
    }
    float x = (float(o.shared.next_random()) * sum) * kInv2p32;
    size_t i = 0;
    while (i < n && !(x <= w_[i])) {
      x = x - w_[i];
      ++i;
    }
    // Past the last entry the original reads beyond its table; the port
    // takes the last entry.
    if (i >= n) i = n - 1;
    const Entry& e = entries_[i];
    out_ = o.read(e.value);
    w_[i] = w_[i] * o.read(e.mult);
    // Quirk kept: whether the increment is a constant is tested on entry j,
    // but the increment is taken from the chosen entry.
    for (size_t j = (i + 1) % n; j != i; j = (j + 1) % n) {
      if (!entries_[j].inc.connected()) w_[j] = e.inc.value + w_[j];
      else w_[j] = w_[j] + o.control(uint32_t(e.inc.src), e.inc.pin);
    }
  }

  Triple min_total_;
  std::vector<Entry> entries_;
  std::vector<float> w_;
  float out_ = 0.0f;
};

}  // namespace

// ParameterIn / ParameterOut / ActionIn are reached by the engine through
// these helpers.
bool parameter_in_set(Component& c, float v) {
  if (auto* p = dynamic_cast<ParameterIn*>(&c)) {
    p->value = v;
    return true;
  }
  return false;
}

float parameter_out_value(Component& c) {
  if (auto* p = dynamic_cast<ParameterOut*>(&c)) return p->value;
  return 0.0f;
}

int32_t action_in_target(Component& c) {
  if (auto* a = dynamic_cast<ActionIn*>(&c)) return a->target;
  return -1;
}

std::unique_ptr<Component> make_control_component(const AobjRecord& r, const Aobj&) {
  switch (r.type) {
    case 0x00: return std::make_unique<ParameterIn>(r);
    case 0x01: return std::make_unique<ParameterOut>(r);
    case 0x02: return std::make_unique<ActionIn>(r);
    case 0x12: return std::make_unique<Variable>(r);
    case 0x13: return std::make_unique<PiecewiseLinear>(r);
    case 0x18: return std::make_unique<ControlLowPass>(r);
    case 0x1A: return std::make_unique<Timer>(r);
    case 0x1D: return std::make_unique<Random>(r);
    case 0x1F: case 0x20: case 0x24: case 0x25: return std::make_unique<Fold>(r);
    case 0x26: return std::make_unique<Gradient>(r);
    case 0x29: return std::make_unique<Sin>(r);
    case 0x2E: return std::make_unique<Envelope>(r);
    case 0x30: return std::make_unique<GateSwitch>(r);
    case 0x31: return std::make_unique<Array>(r);
    case 0xA9: return std::make_unique<Script>(r);
    case 0xAA: return std::make_unique<Selector>(r);
    case 0xAB: return std::make_unique<DebugPrint>(r);
    default: return nullptr;
  }
}

}  // namespace geow2::audio

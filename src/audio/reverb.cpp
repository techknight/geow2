// XAudio's I3DL2 reverb as statically linked into the original. Every operation follows the original's precision:
// single-precision steps, std::fma where the original uses fmadds /
// fnmsubs / fmsub, double where it computes in double.

#include "audio/reverb.h"

#include <algorithm>
#include <bit>
#include <limits>

#include "extract/bytes.h"

namespace geow2::audio {

namespace {

constexpr uint32_t kImageBase = 0x82000000;

// The set-up's tables (absolute addresses, see ReverbTables).
constexpr uint32_t kT1FC = 0x8201F1FC, kT238 = 0x8201F238, kT2B4 = 0x8201F2B4, kT2F0 = 0x8201F2F0;
constexpr uint32_t kT370 = 0x8201F370, kT3F0 = 0x8201F3F0, kT470 = 0x8201F470, kT4F0 = 0x8201F4F0;
constexpr uint32_t kT570 = 0x8201F570, kT778 = 0x8201F778, kT980 = 0x8201F980, kTBA0 = 0x8201FBA0;
constexpr uint32_t kTDC0 = 0x8201FDC0;

// fctidz then stfiwx: truncation toward zero to 64 bits (saturating, NaN
// giving the most negative value), low 32 bits kept.
uint32_t trunc32(double x) {
  int64_t v;
  if (x != x) v = std::numeric_limits<int64_t>::min();
  else if (x >= 9223372036854775807.0) v = std::numeric_limits<int64_t>::max();
  else if (x <= -9223372036854775808.0) v = std::numeric_limits<int64_t>::min();
  else v = int64_t(x);
  return uint32_t(uint64_t(v));
}

// fctiwz: truncation toward zero to 32 bits, saturating (NaN: INT_MIN).
int32_t truncw(double x) {
  if (x != x) return std::numeric_limits<int32_t>::min();
  if (x >= 2147483647.0) return std::numeric_limits<int32_t>::max();
  if (x <= -2147483648.0) return std::numeric_limits<int32_t>::min();
  return int32_t(x);
}

// u32 -> double (fcfid of a zero-extended word) -> single (frsp).
float from_u32(uint32_t v) { return float(double(v)); }

// The table of sub_82152500: 1 then every prime up to 9973 (dat_82020218,
// checked against the image when traced).
const std::array<uint32_t, 1230>& length_table() {
  static const std::array<uint32_t, 1230> t = [] {
    std::array<uint32_t, 1230> out{};
    std::vector<bool> composite(9974, false);
    size_t n = 0;
    out[n++] = 1;
    for (uint32_t i = 2; i <= 9973; ++i) {
      if (composite[i]) continue;
      out[n++] = i;
      for (uint32_t j = i * i; j <= 9973; j += i) composite[j] = true;
    }
    return out;
  }();
  return t;
}

// sub_8212EA80 / sub_82131FE0 / sub_821303B8 are the CRT's cos, log and
// pow; the host's are used (DIFFERENCES.md, Reverb).
double log10_crt(double x) { return std::log(x) * 0.4342944819032518; }  // sub_82132160, dat_8201D3F0

}  // namespace

float ReverbTables::at(uint32_t table, uint32_t index) const {
  const uint32_t off = table + index * 4u - kBase;
  if (off >= (kEnd - kBase)) return 0.0f;
  return words[off / 4];
}

ReverbTables ReverbTables::from_image(std::span<const uint8_t> image) {
  ReverbTables t;
  need(image, kBase - kImageBase, kEnd - kBase, "reverb tables");
  for (size_t i = 0; i < t.words.size(); ++i)
    t.words[i] = std::bit_cast<float>(be32(image.data() + (kBase - kImageBase) + 4 * i));
  return t;
}

uint32_t reverb_length(uint32_t x) {
  if (x <= 1) return 1;
  if (x >= 0x26F5) return 0x26F5;
  const auto& t = length_table();
  uint32_t lo = 0, hi = 0x4CD, prev = 1, mid;
  for (;;) {
    mid = (lo + hi) >> 1;
    if (t[mid] == x) break;
    if (t[mid] > x) hi = mid - 1;
    else lo = mid + 1;
    if (mid == prev) break;
    prev = mid;
  }
  return t[mid];
}

ReverbNative i3dl2_to_native(const I3dl2& p) {
  ReverbNative n;
  n.rear_delay = 5;                 // dat_8201F15C
  n.position_l = n.position_r = 6;  // dat_8201F154
  n.matrix_l = n.matrix_r = 27;     // dat_8201F158
  n.room_size = 100.0f;             // dat_8201F170
  n.room_main = float(double(p.room)) * 0.01f;  // dat_820011B0
  n.low_cutoff = 4;
  n.high_cutoff = 6;
  n.room_hf = float(double(p.room_hf)) * 0.01f;
  if (!(p.decay_hf_ratio < 1.0f)) {
    n.high_gain = 8;
    const int32_t k = truncw(double(float(log10_crt(double(p.decay_hf_ratio)))) * -4.0);  // dat_8201FE78
    n.low_gain = uint32_t(k < -8 ? 0 : k < 0 ? k + 8 : 8);
    n.decay = p.decay_time * p.decay_hf_ratio;
  } else {
    n.low_gain = 8;
    const int32_t k = truncw(double(float(log10_crt(double(p.decay_hf_ratio)))) * 4.0);  // dat_82001BA8
    n.high_gain = uint32_t(k < -8 ? 0 : k < 0 ? k + 8 : 8);
    n.decay = p.decay_time;
  }
  n.reflections_gain = float(double(p.reflections)) * 0.01f;
  float t = p.reflections_delay * 1000.0f;  // dat_82000790
  if (!(t < 300.0f)) t = 300.0f - 1.0f;     // dat_8201F14C
  if (!(t > 1.0f)) t = 1.0f;
  n.reflections_delay = trunc32(t);
  n.reverb_gain = float(double(p.reverb)) * 0.01f;
  t = p.reverb_delay * 1000.0f;
  if (!(t < 85.0f)) t = 85.0f - 1.0f;  // dat_8201F150; no lower clamp
  n.reverb_delay = trunc32(t);
  n.diffusion_early = n.diffusion_late = trunc32(p.diffusion * 0.15f);  // dat_82000D58
  n.density = p.density;
  n.room_freq = p.hf_reference;
  return n;
}

// ---------------------------------------------------------------- stages

void Reverb::Line::init(uint32_t n) {
  buf.assign(n, 0.0f);
  mask = n - 1;
  pos = 0;
}

void Reverb::Xfade::retarget(uint32_t len) {
  // sub_82148590: a new target starts a fade only when none is running.
  target = len;
  if (old == cur) {
    cur = len;
    f = 1.0f;
  }
}

void Reverb::Xfade::run(const float* in, Frame& out, bool copy_path) {
  // sub_821413F0 (copy_path) / sub_82142480.
  constexpr float d = 0.0002f;  // dat_8201EEC8
  const uint32_t n_buf = line.mask + 1;
  const uint32_t w = line.pos;
  out[0] = carry;
  // One sample: reads before the write, the original's two operand orders.
  float g = 1.0f - f;
  auto sample = [&](uint32_t i, bool form_b) {
    const float nw = line.buf[(w + i - cur) & line.mask];
    const float od = line.buf[(w + i - old) & line.mask];
    line.buf[(w + i) & line.mask] = in[i];
    out[i + 1] = form_b ? std::fma(nw, g, od * f) : std::fma(od, f, nw * g);
    g = g + d;
    f = f - d;
  };
  const uint32_t n = trunc32(f * 10000.0f) >> 1;  // dat_82002DB4
  if (n < uint32_t(kFrameSamples)) {
    uint32_t i = 0;
    if (n >= 4)
      for (uint32_t grp = 0; grp < (n >> 2); ++grp)
        for (int j = 0; j < 4; ++j) sample(i++, j != 0);
    while (i < n) sample(i++, false);
    f = target != cur ? 1.0f : 0.0f;
    g = 1.0f - f;
    old = cur;
    cur = target;
    while (i < uint32_t(kFrameSamples)) sample(i++, false);
  } else {
    const uint32_t a = w - old, b = w - cur;
    const bool contiguous = w + 256 < n_buf && a + 256 < n_buf && b + 256 < n_buf && int32_t(a) >= 0 &&
                            int32_t(b) >= 0;
    const uint32_t diff1 = cur - old, diff2 = old - cur;
    if (copy_path && contiguous && diff1 <= 4 && diff2 <= 4) {
      // The vector copy of the old tap (it also moves four words past the
      // frame, which are rewritten before they are read).
      for (uint32_t i = 0; i < uint32_t(kFrameSamples); ++i) {
        out[i + 1] = line.buf[(w + i - old) & line.mask];
        line.buf[(w + i) & line.mask] = in[i];
      }
      f = f - 0.0512f;  // dat_8201EEC4
    } else {
      for (uint32_t i = 0; i < uint32_t(kFrameSamples); ++i) sample(i, false);
    }
  }
  carry = out[kFrameSamples];
  line.pos = (w + uint32_t(kFrameSamples)) & line.mask;
}

void Reverb::RoomFilter::coefficients() {
  // sub_82148130.
  const float s = room_hf + room;
  const float r = s >= 0.0f ? 1.0f : room / s;
  const float w = float(double(freq) * 6.283185308 / double(fs));  // dat_8201FE08
  const float ghf = float(std::pow(double(float(std::pow(10.0, double(((1.0f - r) * s) * 0.1f)))), 0.5));
  g = float(std::pow(double(float(std::pow(10.0, double((r * s) * 0.05f)))), 0.5));  // dat_82000D60
  if (ghf >= 0.995f) {  // dat_8201FE00
    a = 0.0f;
    return;
  }
  const float c = float(std::cos(double(w)));
  const float t1 = ghf * ghf, t2 = c * c, t3 = c * ghf;
  const double fsq = std::fma((1.0 - double(c)) * double(ghf), 2.0, -((1.0 - double(t2)) * double(t1)));
  const float num = float((1.0 - double(t3)) - std::sqrt(fsq));
  a = num / float(1.0 - double(ghf));
}

void Reverb::RoomFilter::run(const Frame& in, Frame& out) {
  // sub_82141A48: two one-poles, the second fed with the first's previous
  // value; the unrolled loop's first sample of each four rounds differently.
  const float one_a = 1.0f - a;
  const float k = g * one_a;
  out[0] = y2;
  for (int i = 0; i < kFrameSamples; ++i) {
    const float x = in[size_t(i)];
    const float y1n = (i & 3) == 0 ? std::fma(a, y1, x * k) : std::fma(x * g, one_a, a * y1);
    const float y2n = std::fma(k, y1, a * y2);
    y1 = y1n;
    y2 = y2n;
    out[size_t(i) + 1] = y2n;
  }
}

void Reverb::Taps::run(const Frame& in, Frame& out1, Frame& out2) {
  out1[0] = carry1;
  out2[0] = carry2;
  for (int i = 0; i < kFrameSamples; ++i) {
    const float a = line.tap(l0), b = line.tap(l1), e = line.tap(l2);
    line.push(in[size_t(i)]);
    out1[size_t(i) + 1] = std::fma(b, c1, a * c0);
    out2[size_t(i) + 1] = e * c2;
  }
  carry1 = out1[kFrameSamples];
  carry2 = out2[kFrameSamples];
}

void Reverb::Taps3::run(const Frame& in, Frame& out1, Frame& out2) {
  out1[0] = carry1;
  out2[0] = carry2;
  for (int i = 0; i < kFrameSamples; ++i) {
    const float a = line.tap(l0), b = line.tap(l1);
    line.push(in[size_t(i)]);
    out1[size_t(i) + 1] = a * c0;
    out2[size_t(i) + 1] = b * c1;
  }
  carry1 = out1[kFrameSamples];
  carry2 = out2[kFrameSamples];
}

void Reverb::Allpass::run_in_place(Frame& p) {
  float prev = carry;
  for (int i = 0; i < kFrameSamples; ++i) {
    const float x = p[size_t(i)];
    p[size_t(i)] = prev;
    prev = step(x);
  }
  p[kFrameSamples] = prev;
  carry = prev;
}

void Reverb::Allpass::run(Frame& in, Frame& out) {
  // sub_82142270 also stores its carry over in[256].
  out[0] = carry;
  for (int i = 0; i < kFrameSamples; ++i) out[size_t(i) + 1] = step(in[size_t(i)]);
  carry = out[kFrameSamples];
  in[kFrameSamples] = carry;
}

// ---------------------------------------------------------------- the effect

Reverb::Reverb(const ReverbTables* tables) : tables_(tables) {
  // sub_821493C0.
  for (Xfade& x : pre_) {
    x.cur = x.old = x.target = 40;
    x.line.init(0x4000);
  }
  for (RoomFilter& r : rlp_) {
    r.fs = fs_;
    r.freq = fs_ * 0.5f;  // dat_82000724
    r.coefficients();
  }
  for (int c = 0; c < 2; ++c) {
    Taps& t1 = t1_[size_t(c)];
    t1.l0 = 509, t1.c0 = 0.2f, t1.l1 = 508, t1.c1 = 0.4f, t1.l2 = 1, t1.c2 = -0.001f;  // dat_8201F178..F180
    t1.line.init(0x200);
    Taps& t2 = t2_[size_t(c)];
    t2.l0 = c == 0 ? 2039 : 1297, t2.c0 = 0.2f, t2.l1 = t2.l0 - 1, t2.c1 = 0.6f, t2.l2 = 600, t2.c2 = 0.05f;
    t2.line.init(0x800);
    ap1_[size_t(c)].len = c == 0 ? 83 : 97;
    ap1_[size_t(c)].set(0.375f);  // dat_8201F184, dat_8201FE74
    ap1_[size_t(c)].line.init(0x80);
    ap2_[size_t(c)].len = c == 0 ? 211 : 223;
    ap2_[size_t(c)].set(0.5f);  // dat_82000724, dat_82001230
    ap2_[size_t(c)].line.init(0x100);
    ap3_[size_t(c)].len = c == 0 ? 311 : 293;
    ap3_[size_t(c)].set(0.5f);
    ap3_[size_t(c)].line.init(0x200);
    Taps3& t3 = t3_[size_t(c)];
    t3.l0 = 1020, t3.c0 = -0.7f, t3.l1 = 1, t3.c1 = 0.04375f;  // dat_8201F19C, dat_8201F1A0
    t3.line.init(0x400);
  }
  auto ap = [](LateAllpass& a, uint32_t len, float c, uint32_t size) {
    a.len = len;
    a.set(c);
    a.line.init(size);
  };
  ap(apa_, 409, 0.618f, 0x200);  // dat_8201F1C4, dat_8201FE70
  ap(apb_, 257, 0.5f, 0x200);
  ap(apc_, 383, 0.618f, 0x200);
  ap(apd_, 233, 0.5f, 0x200);
  ap(ape_, 1511, 0.618f, 0x800);
  ap(apg_, 853, 0.618f, 0x800);
  ap(aph_, 541, 0.5f, 0x400);
  ap(api_, 1657, 0.618f, 0x800);
  ap(apk_, 887, 0.618f, 0x800);
  ap(apl_, 491, 0.5f, 0x400);
  for (LateMix* m : {&apf_, &apj_}) {
    m->k1 = 0.618f, m->k2 = 0.618f, m->k3 = -0.618f;
    m->line.init(0x800);
  }
  apf_.len = 1061;
  apj_.len = 1103;
  auto two = [](TwoTap& t, uint32_t len) {
    t.la = t.lb = len;
    t.ga = 1.0f;
    t.gb = float(std::pow(10.0, 0.0)) * 0.9999f;  // dat_8201F1CC
    t.line.init(0x1000);
  };
  two(dla_, 3432);
  two(dlb_, 3820);
  two(dlc_, 1510);
  two(dld_, 1438);
  for (Biquad* b : {&eq1l_, &eq1r_}) b->c = -0.99845f;  // dat_8201F1AC
  for (Biquad* b : {&eq2l_, &eq2r_}) b->ca = 0.23906f, b->cb = 0.98453f, b->cc = -0.0901f;
  for (Biquad* b : {&eq3l_, &eq3r_}) b->ca = 0.2006f, b->c = 0.76094f;  // cg, cf
  lp1l_.c = lp1r_.c = 0.6f;              // dat_8201F18C
  lp2a_.ca = lp2a_.cb = 0.3f;            // dat_82000708: cx, cy
  lp2b_.ca = lp2b_.cb = -0.3f;           // dat_8201F1F0
  const uint32_t oap_len[4] = {131, 113, 107, 127};
  for (int i = 0; i < 4; ++i) {
    oap_[size_t(i)].len = oap_len[i];
    oap_[size_t(i)].set(0.309f);  // dat_8201F1F4, dat_8201FE6C
    oap_[size_t(i)].line.init(0x200);
  }
  for (Xfade& x : rear_) {
    x.cur = x.old = x.target = 20;
    x.line.init(0x100);
  }
  gr_ = float(std::pow(10.0, 0.0));
}

void Reverb::set_i3dl2(const I3dl2& p) { set_native(i3dl2_to_native(p)); }

void Reverb::set_native(const ReverbNative& n) {
  // sub_82148590, in its order.
  static const ReverbTables kEmpty{};
  const ReverbTables& tb = tables_ ? *tables_ : kEmpty;
  auto len = [](float x) { return reverb_length(trunc32(x)); };

  uint32_t lr = n.reverb_delay == 0 ? 1 : trunc32((fs_ * from_u32(n.reverb_delay)) * 0.001f);  // dat_82001A18
  lr = std::min(reverb_length(lr), 0xFFFu);
  dla_.lb = dlb_.lb = dlc_.lb = dld_.lb = lr;

  const float q = !(n.room_size < 50.0f) ? (n.room_size * 0.01f) * fs_  // dat_82001040
                                         : (std::fma(n.room_size, 0.4f, 30.0f) * 0.01f) * fs_;  // dat_8201F17C, dat_82000CDC
  float s1 = q * 2.0833333e-5f;  // dat_8201C9D0
  if (!(s1 >= 0.3f)) s1 = 0.3f;  // dat_82000708
  float s2 = s1 * 2.0f;          // dat_820006FC
  if (!(s2 >= 0.3f)) s2 = 0.3f;
  else if (s2 > 1.0f) s2 = 1.0f;

  const float dn = n.density;
  float d75 = 0.0f, d50 = 0.0f, d25 = 0.0f, d0 = 0.0f;
  if (!(dn < 75.0f)) d75 = (dn - 75.0f) * 0.04f, d50 = d25 = d0 = 1.0f;  // dat_82000F74, dat_82001BB0
  else if (!(dn < 50.0f)) d50 = (dn - 50.0f) * 0.04f, d25 = d0 = 1.0f;
  else if (!(dn < 25.0f)) d25 = (dn - 25.0f) * 0.04f, d0 = 1.0f;  // dat_82000D6C
  else d0 = dn * 0.04f;

  // Lengths (dat_8201FE20..8201FE68).
  t1_[0].l0 = len(s2 * 509.0f), t1_[0].l1 = t1_[0].l0 - 1;
  t2_[0].l0 = len(s2 * 2039.0f), t2_[0].l1 = t2_[0].l0 - 1;
  const uint32_t l3 = len(from_u32(n.reverb_delay != 0 ? 9u : 1020u) * s2);
  t3_[0].l0 = l3;
  t1_[1].l0 = len(s2 * 509.0f), t1_[1].l1 = t1_[1].l0 - 1;
  t2_[1].l0 = len(s2 * 1297.0f), t2_[1].l1 = t2_[1].l0 - 1;
  t3_[1].l0 = l3;
  const float p75 = d75 * s2;
  apa_.len = len(p75 * 409.0f);
  apb_.len = len(s2 * 257.0f);
  dla_.la = len((d75 * s1) * 3432.0f);
  apc_.len = len(p75 * 383.0f);
  apd_.len = len(s2 * 233.0f);
  dlb_.la = len((d50 * s1) * 3820.0f);
  const float p25 = d25 * s2;
  ape_.len = len(p25 * 1511.0f);
  const float p50 = d50 * s2;
  apf_.len = len(p50 * 1061.0f) - 1;  // the only length stored minus one
  apg_.len = len(s2 * 853.0f);
  aph_.len = len(p75 * 541.0f);
  dlc_.la = len((d0 * s1) * 1510.0f);
  api_.len = len(p25 * 1657.0f);
  apj_.len = len(p50 * 1103.0f);
  apk_.len = len(p75 * 887.0f);
  apl_.len = len(p75 * 491.0f);
  dld_.la = len(s1 * 1438.0f);

  // The decay gain: from the mean of the sixteen late lengths.
  const uint32_t sum = apd_.len + api_.len + apc_.len + apl_.len + apk_.len + dlc_.la + dla_.la + apj_.len +
                       apg_.len + apf_.len + apb_.len + aph_.len + apa_.len + ape_.len + dld_.la + dlb_.la;
  const float m = from_u32(sum >> 1) / fs_;
  const double pw = std::pow(10.0, -3.0 / (double(n.decay) / double(m)));  // dat_82000F80, dat_8201FE18
  const float g = float(std::sqrt(pw * 0.5));                               // fsqrts; dat_82002370
  const float h = std::fma(-g, 0.63f, 0.9f);                                // fnmsubs; dat_8201FE14, dat_82000D70

  const float ce = tb.at(kTDC0, n.diffusion_early), cl = tb.at(kTDC0, n.diffusion_late);
  const uint32_t lo = n.low_gain * 10u + n.low_cutoff, hi = n.high_gain * 15u + n.high_cutoff;

  for (Xfade& x : pre_) x.retarget(n.reflections_delay == 0 ? 1 : trunc32((from_u32(n.reflections_delay) * fs_) * 0.001f));

  for (int c = 0; c < 2; ++c) {
    t1_[size_t(c)].c0 = 0.2f;
    t1_[size_t(c)].c1 = 0.4f;
    t2_[size_t(c)].c0 = 0.2f;
    t2_[size_t(c)].c1 = 0.6f;
    ap1_[size_t(c)].set(ce);
    ap2_[size_t(c)].set(ce);
    ap3_[size_t(c)].set(ce);
  }
  t1_[0].c2 = tb.at(kT370, n.position_l);
  t1_[1].c2 = tb.at(kT370, n.position_r);
  t2_[0].c2 = tb.at(kT4F0, n.matrix_l) * -0.125f;  // dat_8201FE10
  t2_[1].c2 = tb.at(kT4F0, n.matrix_r) * -0.125f;
  t3_[0].c0 = tb.at(kT238, n.matrix_l) * h;
  t3_[0].c1 = tb.at(kT2F0, n.matrix_l) * h;
  t3_[1].c0 = tb.at(kT238, n.matrix_r) * h;
  t3_[1].c1 = tb.at(kT2F0, n.matrix_r) * h;
  k_r1_ = tb.at(kT3F0, n.position_r);
  k_l2_ = tb.at(kT470, n.position_l);
  k_l1_ = tb.at(kT3F0, n.position_l);
  k_r2_ = tb.at(kT470, n.position_r);
  for (LateAllpass* a : {&apa_, &apb_, &apc_, &apd_, &aph_, &apl_}) a->set(cl);
  for (Biquad* b : {&eq1l_, &eq1r_}) b->c = tb.at(kT570, lo);
  for (Biquad* b : {&eq2l_, &eq2r_}) {
    b->ca = tb.at(kT2B4, n.high_cutoff);
    b->cb = tb.at(kT778, lo);
    b->cc = tb.at(kT980, hi);
  }
  for (Biquad* b : {&eq3l_, &eq3r_}) {
    b->ca = tb.at(kTBA0, hi);           // cg
    b->c = tb.at(kT1FC, n.high_cutoff);  // cf
  }
  lp1l_.c = lp1r_.c = g;
  lp2a_.ca = lp2a_.cb = g * 0.5f;
  lp2b_.ca = lp2b_.cb = -(g * 0.5f);

  const float gv = float(std::pow(10.0, double(n.reverb_gain) * 0.05));  // dat_82010170
  dla_.gb = gv * tb.at(kT4F0, n.matrix_l);
  dlb_.gb = gv * tb.at(kT4F0, n.matrix_r);
  dlc_.gb = gv * ((tb.at(kT2F0, n.matrix_l) * h) * -0.125f);
  dld_.gb = gv * ((tb.at(kT2F0, n.matrix_r) * h) * -0.125f);
  gr_ = float(std::pow(10.0, double(n.reflections_gain) * 0.05));

  const float co = cl * 0.6f;
  for (Allpass& a : oap_) a.set(co);

  for (RoomFilter& r : rlp_) {
    r.freq = n.room_freq;
    r.room = n.room_main;
    r.room_hf = n.room_hf;
    r.coefficients();
  }
  for (Xfade& x : rear_) x.retarget(n.rear_delay == 0 ? 1 : trunc32((from_u32(n.rear_delay) * fs_) * 0.001f));
}

void Reverb::process(const float* l, const float* r, Output& out) {
  // sub_821429F0: the two channel chains.
  for (int c = 0; c < 2; ++c) {
    const size_t k = size_t(c);
    pre_[k].run(c == 0 ? l : r, p_, true);
    rlp_[k].run(p_, r_);
    t1_[k].run(r_, a_[k], b_[k]);
    ap1_[k].run_in_place(a_[k]);
    t2_[k].run(a_[k], c_[k], d_[k]);
    ap2_[k].run_in_place(c_[k]);
    ap3_[k].run(c_[k], e_);
    t3_[k].run(e_, f_[k], g_[k]);
  }
  // The early mix (loop 82142E90, unrolled by 8; lane 1 rounds differently).
  for (int i = 0; i < kFrameSamples; ++i) {
    const size_t s = size_t(i);
    el_[s] = (i & 7) == 1 ? gr_ * std::fma(k_l2_, c_[0][s], std::fma(a_[1][s], k_l1_, b_[0][s]))
                          : gr_ * (std::fma(a_[1][s], k_l1_, c_[0][s] * k_l2_) + b_[0][s]);
    er_[s] = gr_ * (std::fma(a_[0][s], k_r1_, c_[1][s] * k_r2_) + b_[1][s]);
  }
  late(f_[0], f_[1], g_[0], g_[1], d_[0], d_[1]);
  oap_[0].run_in_place(ol_);
  oap_[1].run_in_place(ol_);
  oap_[2].run_in_place(or_);
  oap_[3].run_in_place(or_);
  rear_[0].run(ql_in_.data(), ql_, false);
  rear_[1].run(qr_in_.data(), qr_, false);
  // The output mix (VMX): with w = 1, FL and FR are the late outputs.
  const float dry = 1.0f - wet_, wet2 = wet_ + wet_;
  for (int i = 0; i < kFrameSamples; ++i) {
    const size_t s = size_t(i);
    out.fl[s] = std::fma(dry, l[s], wet_ * ol_[s]);
    out.fr[s] = std::fma(dry, r[s], wet_ * or_[s]);
    out.bl[s] = wet2 * ql_[s];
    out.br[s] = wet2 * qr_[s];
  }
}

void Reverb::late(const Frame& f0, const Frame& f1, const Frame& g0, const Frame& g1, const Frame& d0,
                  const Frame& d1) {
  // The loop at 82143478: every right-hand state value is the one from
  // before this sample; the outputs go to OL/OR (over F0/F1 in the
  // original) and the rear sends to G0/G1, read by the rear delays.
  for (int i = 0; i < kFrameSamples; ++i) {
    const size_t s = size_t(i);
    const float lp2b = lp2b_.y, lp2a = lp2a_.y, ya = apa_.y, yb = apb_.y, yc = apc_.y, yd = apd_.y;
    const float ye = ape_.y, yf = apf_.y, yg = apg_.y, yh = aph_.y, yi = api_.y, yj = apj_.y, yk = apk_.y,
                yl = apl_.y;
    const float q1l = eq1l_.y, q2l = eq2l_.y, q3l = eq3l_.y, q1r = eq1r_.y, q2r = eq2r_.y, q3r = eq3r_.y;
    const float lp1l = lp1l_.y, lp1r = lp1r_.y;
    const float aA = dla_.ya, aB = dla_.yb, bA = dlb_.ya, bB = dlb_.yb, cA = dlc_.ya, cB = dlc_.yb,
                dA = dld_.ya, dB = dld_.yb;

    // Left pre-path.
    {
      LateAllpass& a = apa_;
      const float d = a.line.tap(a.len);
      const float v = (std::fma(a.c, d, f0[s]) + lp2b) + lp2a;
      a.line.push(v);
      a.y = std::fma(a.neg, v, d);
    }
    {
      LateAllpass& a = apb_;
      const float d = a.line.tap(a.len);
      const float v = std::fma(a.c, d, ya);
      a.line.push(v);
      a.y = std::fma(v, a.neg, d);
    }
    {
      const float u = d0[s] + yb, t = eq1l_.z2;
      eq1l_.z2 = eq1l_.z1, eq1l_.z1 = u;
      eq1l_.y = std::fma(t, eq1l_.c, u);
    }
    {
      const float t = eq2l_.z2, v = std::fma(eq2l_.cb, t, q1l);
      eq2l_.z2 = eq2l_.z1, eq2l_.z1 = v;
      eq2l_.y = std::fma(v, eq2l_.ca, eq2l_.cc * t);
    }
    {
      const float t = eq3l_.z2, v = std::fma(t, eq3l_.c, q2l);
      eq3l_.z2 = eq3l_.z1, eq3l_.z1 = v;
      eq3l_.y = eq3l_.ca * v;
    }
    dla_.ya = dla_.line.tap(dla_.la) * dla_.ga;
    dla_.yb = dla_.line.tap(dla_.lb) * dla_.gb;
    dla_.line.push(q3l * 8.0f);  // dat_82000D18
    lp1l_.y = lp1l_.z2 * lp1l_.c;
    lp1l_.z2 = lp1l_.z1, lp1l_.z1 = aA;

    // Right pre-path.
    {
      LateAllpass& a = apc_;
      const float d = a.line.tap(a.len);
      const float v = std::fma(a.c, d, lp2b - lp2a) + f1[s];
      a.line.push(v);
      a.y = std::fma(v, a.neg, d);
    }
    {
      LateAllpass& a = apd_;
      const float d = a.line.tap(a.len);
      const float v = std::fma(a.c, d, yc);
      a.line.push(v);
      a.y = std::fma(v, a.neg, d);
    }
    {
      const float u = d1[s] + yd, t = eq1r_.z2;
      eq1r_.z2 = eq1r_.z1, eq1r_.z1 = u;
      eq1r_.y = std::fma(t, eq1r_.c, u);
    }
    {
      const float t = eq2r_.z2, v = std::fma(eq2r_.cb, t, q1r);
      eq2r_.z2 = eq2r_.z1, eq2r_.z1 = v;
      eq2r_.y = std::fma(t, eq2r_.cc, v * eq2r_.ca);  // rounds unlike the left one
    }
    {
      const float t = eq3r_.z2, v = std::fma(t, eq3r_.c, q2r);
      eq3r_.z2 = eq3r_.z1, eq3r_.z1 = v;
      eq3r_.y = v * eq3r_.ca;
    }
    dlb_.yb = dlb_.gb * dlb_.line.tap(dlb_.lb);
    dlb_.ya = dlb_.line.tap(dlb_.la) * dlb_.ga;
    dlb_.line.push(q3r * 8.0f);
    lp1r_.y = lp1r_.z2 * lp1r_.c;
    lp1r_.z2 = lp1r_.z1, lp1r_.z1 = bA;

    // Cross paths.
    {
      LateAllpass& a = ape_;
      const float d = a.line.tap(a.len);
      const float v = (std::fma(d, a.c, g1[s]) + lp1r) + lp1l;
      a.line.push(v);
      a.y = std::fma(v, a.neg, d);
    }
    {
      LateMix& m = apf_;
      const float d = m.line.tap(m.len), u = g0[s] + ye;
      m.line.push(std::fma(d, m.k2, u));
      m.y = std::fma(m.k1, u, d * m.k3);
    }
    {
      LateAllpass& a = apg_;
      const float d = a.line.tap(a.len);
      const float v = std::fma(d, a.c, yf);
      a.line.push(v);
      a.y = std::fma(v, a.neg, d);
    }
    {
      LateAllpass& a = aph_;
      const float d = a.line.tap(a.len);
      const float v = std::fma(d, a.c, g0[s]) + yg;
      a.line.push(v);
      a.y = std::fma(v, a.neg, d);
    }
    dlc_.yb = dlc_.line.tap(dlc_.lb) * dlc_.gb;
    dlc_.ya = dlc_.line.tap(dlc_.la) * dlc_.ga;
    dlc_.line.push(d0[s] + yh);
    {
      const float t = lp2a_.z2;
      lp2a_.z2 = lp2a_.z1, lp2a_.z1 = cA;
      lp2a_.y = std::fma(t, lp2a_.cb, lp2a_.ca * cA);
    }
    {
      LateAllpass& a = api_;
      const float d = a.line.tap(a.len);
      const float v = std::fma(d, a.c, lp1r - lp1l) + g0[s];
      a.line.push(v);
      a.y = std::fma(v, a.neg, d);
    }
    {
      LateMix& m = apj_;
      const float d = m.line.tap(m.len), u = g1[s] + yi;
      m.line.push(std::fma(d, m.k2, u));
      m.y = std::fma(u, m.k1, d * m.k3);
    }
    {
      LateAllpass& a = apk_;
      const float d = a.line.tap(a.len);
      const float v = std::fma(a.c, d, yj);
      a.line.push(v);
      a.y = std::fma(v, a.neg, d);
    }
    {
      LateAllpass& a = apl_;
      const float d = a.line.tap(a.len);
      const float v = std::fma(d, a.c, g1[s]) + yk;
      a.line.push(v);
      a.y = std::fma(v, a.neg, d);
    }
    dld_.yb = dld_.line.tap(dld_.lb) * dld_.gb;
    dld_.ya = dld_.line.tap(dld_.la) * dld_.ga;
    dld_.line.push(d1[s] + yl);
    {
      const float t = lp2b_.z2;
      lp2b_.z2 = lp2b_.z1, lp2b_.z1 = dA;
      lp2b_.y = std::fma(t, lp2b_.cb, lp2b_.ca * dA);
    }

    or_[s] = ((er_[s] + cB) + bB) * 2.0f;  // dat_820006FC
    ol_[s] = ((el_[s] + dB) + aB) * 2.0f;
    ql_in_[s] = aB;
    qr_in_[s] = bB;
  }
}

}  // namespace geow2::audio

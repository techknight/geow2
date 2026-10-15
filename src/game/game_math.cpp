#include "game/game_math.h"

#include <cmath>
#include <cstring>
#include <stdexcept>

namespace geow2 {
namespace {

constexpr uint32_t kBase = 0x82000000;
constexpr uint32_t kTable = 0x82197D20;  // lis/addi in both functions
constexpr uint32_t kOne = 0x82000C78;

uint64_t be64(std::span<const uint8_t> img, uint32_t addr) {
  const uint32_t o = addr - kBase;
  if (o + 8 > img.size()) throw std::runtime_error("game math: constant outside the image");
  uint64_t v = 0;
  for (int i = 0; i < 8; ++i) v = (v << 8) | img[o + i];
  return v;
}

double f64_at(std::span<const uint8_t> img, uint32_t addr) {
  const uint64_t b = be64(img, addr);
  double d;
  std::memcpy(&d, &b, 8);
  return d;
}

float f32_at(std::span<const uint8_t> img, uint32_t addr) {
  const uint32_t o = addr - kBase;
  if (o + 4 > img.size()) throw std::runtime_error("game math: constant outside the image");
  const uint32_t b = (uint32_t(img[o]) << 24) | (uint32_t(img[o + 1]) << 16) | (uint32_t(img[o + 2]) << 8) | img[o + 3];
  float f;
  std::memcpy(&f, &b, 4);
  return f;
}

// fctid in the default rounding mode: round to nearest, ties to even.
double round_nearest(double v) { return std::nearbyint(v); }

}  // namespace

GameMath GameMath::from_image(std::span<const uint8_t> image) {
  Constants c{};
  c.c00 = f64_at(image, kTable + 0x00);
  c.c08 = f64_at(image, kTable + 0x08);
  c.c10 = f64_at(image, kTable + 0x10);
  c.c28 = f64_at(image, kTable + 0x28);
  c.c30 = f64_at(image, kTable + 0x30);
  for (int i = 0; i < 8; ++i) c.poly[i] = f64_at(image, kTable + 0x38 + uint32_t(i) * 8);
  c.one = f64_at(image, kOne);
  c.past_range = f64_at(image, 0x82197FB0);
  c.c18 = f32_at(image, kTable + 0x18);
  c.c1c = f32_at(image, kTable + 0x1C);
  c.c20 = f32_at(image, kTable + 0x20);
  c.c24 = f32_at(image, kTable + 0x24);
  constexpr uint32_t kAtan = 0x82197DA0;
  c.at_half_pi = f64_at(image, kAtan + 0x08);
  c.at_pi = f64_at(image, kAtan + 0x10);
  c.at_threshold = f64_at(image, kAtan + 0x18);
  c.at_root3 = f64_at(image, kAtan + 0x28);
  for (int i = 0; i < 4; ++i) {
    c.at_p[i] = f64_at(image, kAtan + 0x38 + uint32_t(i) * 8);
    c.at_q[i] = f64_at(image, kAtan + 0x58 + uint32_t(i) * 8);
    c.at_base[i] = f64_at(image, 0x82197E20 + uint32_t(i) * 8);
  }
  c.at_zero = f32_at(image, kAtan + 0xA8);
  c.at_one = f32_at(image, kAtan + 0xB0);
  c.tan_k = f64_at(image, 0x8201D358);
  c.tan_range = f64_at(image, 0x8201D368);
  c.tan_hi = f64_at(image, 0x82197F50);
  c.tan_lo = f64_at(image, 0x82197F58);
  c.tan_a = f64_at(image, 0x8201D3C8);
  c.tan_b = f64_at(image, 0x8201D378);
  c.tan_c = f64_at(image, 0x8201D3A8);
  c.tan_d = f64_at(image, 0x8201D3C0);
  c.tan_e = f64_at(image, 0x8201D3B8);
  c.tan_f = f64_at(image, 0x8201D398);
  c.tan_g = f64_at(image, 0x8201D3B0);
  c.tan_one = f64_at(image, 0x8201D388);
  return GameMath(c);
}

double GameMath::tan(double x) const {
  // sub_82130A98: returns x itself when it is exactly 0.
  if (x == 0.0) return x;
  const double kf = round_nearest(x * k_.tan_k);
  double r = std::fma(-k_.tan_hi, kf, x);  // fnmsub
  r = std::fma(-k_.tan_lo, kf, r);         // fnmsub
  const double z = r * r;
  double a = std::fma(-z, k_.tan_a, k_.tan_b);  // fnmsub: b - z * a
  double b = std::fma(z, k_.tan_c, -k_.tan_d);  // fmsub
  a = std::fma(a, z, -k_.tan_e);                // fmsub
  b = std::fma(b, z, k_.tan_f);                 // fmadd
  const double p = std::fma(a, z, k_.tan_one);  // fmadd
  b = std::fma(b, z, -k_.tan_g);                // fmsub
  const double n = p * r;
  const double q = std::fma(b, z, k_.tan_one);  // fmadd
  const double v = (int64_t(kf) & 1) ? q / -n : n / q;
  return std::fabs(x) - k_.tan_range >= 0.0 ? k_.past_range : v;  // fsel
}

double GameMath::atan2(double y, double x) const {
  // sub_8212FE70: y in f1, x in f2.
  const double zero = k_.at_zero;
  double r;
  if (x == zero) {
    if (y == zero) {
      if (!std::signbit(x)) return y;
      return std::signbit(y) ? -k_.at_pi : k_.at_pi;
    }
    r = k_.at_half_pi;
  } else {
    const double ax = std::fabs(x), ay = std::fabs(y);
    int index = 0;
    double num = ay, den = ax;
    if (ay > ax) {
      index = 2;
      num = ax;
      den = ay;
    }
    double t = num / den;  // fdiv
    if (t > k_.at_threshold) {
      ++index;
      const double sum = k_.at_root3 + t;                                // fadd
      t = std::fma(k_.at_root3, t, -double(k_.at_one)) / sum;            // fmsub, fdiv
    }
    const double z = t * t;
    double q = k_.at_q[3] + z;  // fadd
    double p = std::fma(k_.at_p[3], z, k_.at_p[2]);
    q = std::fma(q, z, k_.at_q[2]);
    p = std::fma(p, z, k_.at_p[1]);
    q = std::fma(q, z, k_.at_q[1]);
    p = std::fma(p, z, k_.at_p[0]);
    q = std::fma(q, z, k_.at_q[0]);
    r = ((p * z) * t) / q + t;
    if (index > 1) r = -r;
    r = k_.at_base[index] + r;
  }
  r = x >= 0.0 ? r : k_.at_pi - r;  // fsel on x
  return std::signbit(y) ? -r : r;
}

double GameMath::poly(double r) const {
  // fmul z = r * r; fmadd chain from +0x70 down to +0x38, then 1.0; times r.
  const double z = r * r;
  double p = k_.poly[7];
  for (int i = 6; i >= 0; --i) p = std::fma(p, z, k_.poly[i]);
  p = std::fma(p, z, k_.one);
  return p * r;
}

double GameMath::sin(double x) const {
  // sub_8212E9B0.
  const double a = std::fabs(x);
  const double sign = x >= 0.0 ? double(k_.c1c) : double(k_.c20);  // fsel on x
  const double kf = round_nearest(k_.c08 * a);
  double r = std::fma(-k_.c28, kf, a);  // fnmsub: a - c28 * k
  r = std::fma(-k_.c30, kf, r);         // fnmsub: r - c30 * k
  double v = poly(r);
  if (int64_t(kf) & 1) v = -v;  // fctidz of the rounded value
  v = v * sign;
  if (a == 0.0) return x;
  return a - k_.c10 >= 0.0 ? k_.past_range : v;  // fsel
}

double GameMath::cos(double x) const {
  // sub_8212EA80.
  const double a = std::fabs(x);
  const double t = k_.c00 + a;
  const double kf = round_nearest(k_.c08 * t);
  const double kh = kf - double(k_.c24);
  double r = std::fma(-k_.c28, kh, a);
  r = std::fma(-k_.c30, kh, r);
  double v = poly(r);
  if (int64_t(kf) & 1) v = -v;
  if (a == double(k_.c18)) return double(k_.c1c);
  return t - k_.c10 >= 0.0 ? k_.past_range : v;
}

}  // namespace geow2

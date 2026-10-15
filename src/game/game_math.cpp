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
  c.past_range = f64_at(image, kTable + 0x7FB0);
  c.c18 = f32_at(image, kTable + 0x18);
  c.c1c = f32_at(image, kTable + 0x1C);
  c.c20 = f32_at(image, kTable + 0x20);
  c.c24 = f32_at(image, kTable + 0x24);
  return GameMath(c);
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

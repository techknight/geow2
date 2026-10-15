#include "game/display_gamma.h"

#include <cmath>

namespace geow2 {
namespace {

// sub_820AACC0(x, 1): sRGB to linear.
float srgb_decode(float x) {
  if (double(x) > 0.04045)  // dat_82007890 (f64)
    return float(std::pow(double((x + 0.055f) * 0.947867334f), double(2.4f)));  // dat_82007888, 78, 80
  return x * 0.0773993805f;  // dat_8200788C
}

// sub_820AACC0(x, 0): linear to sRGB.
float srgb_encode(float x) {
  if (double(x) > 0.0031308)  // dat_82007870 (f64)
    return std::fmaf(float(std::pow(double(x), 0.4166666567325592)), 1.05499995f, -0.055f);  // dat_82007860, 5C, 88
  return x * 12.9200001f;  // dat_82007868
}

// sub_820AAD70(x, 0): linear to BT.709.
float bt709_encode(float x) {
  if (double(x) >= 0.018)  // dat_820078A8 (f64)
    return std::fmaf(float(std::pow(double(x), double(0.45f))), 1.09899998f, -0.0989999995f);  // A0, 98, C0
  return x * 4.5f;  // dat_820018F0
}

}  // namespace

std::array<uint16_t, 1024> display_gamma_table(DisplayGamma type, float power) {
  std::array<uint16_t, 1024> t{};
  for (int i = 0; i < 1024; ++i) {
    // x = i * (1 / 1023) (dat_820078D0), as the image's sRGB value.
    float v = srgb_decode(float(i) * 0.000977517106f);
    switch (type) {
      case DisplayGamma::kSrgb: v = srgb_encode(v); break;
      case DisplayGamma::kPower: v = float(std::pow(double(v), double(1.0f / power))); break;
      default: v = bt709_encode(v); break;
    }
    // fctiwz(fma(v, 1023.0, 0.5)) clamped to 10 bits (dat_820078D8, dat_82002370).
    const int q = int(std::fma(double(v), 1023.0, 0.5));
    t[size_t(i)] = uint16_t(q < 0 ? 0 : (q > 1023 ? 1023 : q));
  }
  return t;
}

std::array<float, 256> display_gamma_ramp(DisplayGamma type, float power) {
  const std::array<uint16_t, 1024> t = display_gamma_table(type, power);
  std::array<float, 256> r{};
  for (int k = 0; k < 256; ++k) r[size_t(k)] = float(t[size_t((k * 257) >> 6)]) / 1023.0f;
  return r;
}

}  // namespace geow2

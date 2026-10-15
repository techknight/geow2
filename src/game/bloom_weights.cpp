#include "game/bloom_weights.h"

#include <cmath>

namespace geow2 {
namespace {

// sub_82067850(x, y, sigma) with y = 0.0, in double: a normal density.
double gauss(double x, double sigma) {
  constexpr double kTwo = 2.0;                   // dat_82001180 (f64)
  constexpr double kTwoPi = 6.2831854820251465;  // dat_82002DA8 (f64; 2 pi as a float)
  const double s2 = sigma * sigma;
  const double r2 = std::fma(x, x, 0.0 * 0.0);
  return std::exp(-(r2 / (s2 * kTwo))) / std::sqrt(s2 * kTwoPi);
}

}  // namespace

std::array<float, 8> blur_weights(float radius, float precision) {
  // sub_82064848: the density at -7..7 summed from 0.0 (dat_82000D30),
  // then w_k = density(k) * (precision / sum) for k = 0..7.
  double sum = 0.0;
  for (int i = -7; i <= 7; ++i) sum = gauss(double(i), radius) + sum;
  const double scale = double(precision) / sum;
  std::array<float, 8> w{};
  for (int k = 0; k < 8; ++k) w[k] = float(gauss(double(k), radius) * scale);
  return w;
}

std::array<float, 5> combine_weights(float precision, float falloff) {
  // sub_82064F88: w_k = (1 / falloff^k) / precision^(2k), k = 1..5; the
  // powers in double (sub_821303B8), rounded to float, then single
  // divisions (fdivs). Exponents dat_82000C78, dat_82001180, dat_82001220,
  // dat_82001BA8, dat_82002A88 and dat_82001180, dat_82001BA8,
  // dat_82002A80, dat_82002A78, dat_82000F80.
  std::array<float, 5> w{};
  for (int k = 1; k <= 5; ++k) {
    const float fall = 1.0f / float(std::pow(double(falloff), double(k)));
    w[k - 1] = fall / float(std::pow(double(precision), double(2 * k)));
  }
  return w;
}

}  // namespace geow2

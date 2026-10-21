#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

namespace geow2 {

// The single-precision fused multiply-adds (fmadds, fmsubs, fnmadds,
// fnmsubs): one rounding of the exact a * b + c, as the PowerPC defines
// them. emulator_rounding (the --emulator-rounding switch, for comparing
// recorded games with Xenia) rounds twice instead, first to double then
// to single, as that emulator does: it differs only when a * b + c lies
// within a hair of halfway between two floats.
inline bool emulator_rounding = false;

inline float fmadds(float a, float b, float c) {
  if (emulator_rounding) return float(std::fma(double(a), double(b), double(c)));
  return std::fmaf(a, b, c);
}

// -(a * b - c): fmadds(-a, b, c) but for the sign of an exact zero.
inline float fnmsubs(float a, float b, float c) {
  if (emulator_rounding) return -float(std::fma(double(a), double(b), -double(c)));
  return -std::fmaf(a, b, -c);
}

// Xenia's frsqrte (its x64 backend's helper): for a positive normal x, an
// 8-bit fraction from a 16-entry table indexed by the exponent's parity
// and the top three fraction bits, exponent 1022 - (e - 1023) / 2. The
// console's own estimate is not documented; the port's normal mode uses
// the exact 1 / sqrt (DIFFERENCES.md, Reciprocal estimates). Other inputs
// fall back to the exact value.
inline double emulator_frsqrte(double x) {
  static constexpr uint8_t kTable[16] = {241, 216, 192, 168, 152, 136, 128, 112,
                                         96,  76,  60,  48,  32,  24,  16,  8};
  uint64_t b;
  std::memcpy(&b, &x, 8);
  const uint64_t biased = (b >> 52) & 0x7FF;
  if ((b >> 63) || biased == 0 || biased == 0x7FF) return 1.0 / std::sqrt(x);
  const uint32_t index = ((uint32_t(biased & 1) << 3) | uint32_t((b >> 49) & 7)) ^ 8;
  const int64_t e = int64_t(biased) - 1023;
  const uint64_t exponent = uint64_t(1022 - (e >> 1)) & 0x7FF;
  const uint64_t r = (exponent << 52) | (uint64_t(kTable[index]) << 44);
  double out;
  std::memcpy(&out, &r, 8);
  return out;
}

}  // namespace geow2

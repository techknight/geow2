#pragma once

#include <cstdint>
#include <span>

namespace geow2 {

// The original's own sin, cos and atan2 (sub_8212E9B0, sub_8212EA80,
// sub_8212FE70): argument reduction and polynomials evaluated in double
// with fused multiply-adds. Their constants are read from the user's
// decoded executable (the cache's xex/image.bin), from the tables at
// 0x82197D20 / 0x82197DA0 / 0x82197E20, so nothing of the original is
// stored in this repository.
class GameMath {
 public:
  struct Constants {
    double c00, c08, c10, c28, c30;  // table + 0x00, 0x08, 0x10, 0x28, 0x30
    double poly[8];                  // table + 0x38 .. 0x70 (poly[0] = +0x38)
    double one;                      // 0x82000C78
    double past_range;               // table + 0x7FB0
    float c18, c1c, c20, c24;        // table + 0x18, 0x1C, 0x20, 0x24 (f32)
    // atan2 (sub_8212FE70), table at 0x82197DA0:
    double at_half_pi, at_pi, at_threshold, at_root3;  // +0x08, +0x10, +0x18, +0x28
    double at_p[4];                                    // +0x38, +0x40, +0x48, +0x50
    double at_q[4];                                    // +0x58, +0x60, +0x68, +0x70
    float at_zero, at_one;                             // +0xA8, +0xB0 (f32)
    double at_base[4];                                 // 0x82197E20: angle per reduction case
  };

  explicit GameMath(const Constants& c) : k_(c) {}
  // Reads the constants from a memory image loaded at 0x82000000.
  static GameMath from_image(std::span<const uint8_t> image);

  double sin(double x) const;
  double cos(double x) const;
  double atan2(double y, double x) const;  // sub_8212FE70(y, x)

  // Single-precision helpers as the callers use them (result rounded with frsp).
  float sinf(float x) const { return float(sin(x)); }
  float cosf(float x) const { return float(cos(x)); }

  const Constants& constants() const { return k_; }

 private:
  double poly(double r) const;
  Constants k_;
};

}  // namespace geow2

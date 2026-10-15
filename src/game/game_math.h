#pragma once

#include <cstdint>
#include <span>

namespace geow2 {

// The original's own sin and cos (sub_8212E9B0, sub_8212EA80): argument
// reduction and a polynomial evaluated in double with fused multiply-adds.
// Their constants are read from the user's decoded executable (the cache's
// xex/image.bin), from the table at 0x82197D20, so nothing of the
// original is stored in this repository.
class GameMath {
 public:
  struct Constants {
    double c00, c08, c10, c28, c30;  // table + 0x00, 0x08, 0x10, 0x28, 0x30
    double poly[8];                  // table + 0x38 .. 0x70 (poly[0] = +0x38)
    double one;                      // 0x82000C78
    double past_range;               // table + 0x7FB0
    float c18, c1c, c20, c24;        // table + 0x18, 0x1C, 0x20, 0x24 (f32)
  };

  explicit GameMath(const Constants& c) : k_(c) {}
  // Reads the constants from a memory image loaded at 0x82000000.
  static GameMath from_image(std::span<const uint8_t> image);

  double sin(double x) const;
  double cos(double x) const;

  // Single-precision helpers as the callers use them (result rounded with frsp).
  float sinf(float x) const { return float(sin(x)); }
  float cosf(float x) const { return float(cos(x)); }

  const Constants& constants() const { return k_; }

 private:
  double poly(double r) const;
  Constants k_;
};

}  // namespace geow2

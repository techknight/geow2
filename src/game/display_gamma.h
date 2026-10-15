#pragma once

#include <array>
#include <cstdint>

namespace geow2 {

// The display types the console reports (VdGetCurrentDisplayGamma).
enum class DisplayGamma { kSrgb = 1, kBt709 = 2, kPower = 3 };

// The console's output gamma table (sub_820AAE20 with mode 0): 1024
// entries, the image taken as sRGB-encoded and re-encoded for the
// display type, as 10-bit values.
std::array<uint16_t, 1024> display_gamma_table(DisplayGamma type, float power);

// The hardware ramp D3D sets at device creation when the game sets none
// (sub_820AE1A0, sub_820AAFB0): for each 8-bit value k, the table entry
// at (k * 257) >> 6, as a fraction of 1023.
std::array<float, 256> display_gamma_ramp(DisplayGamma type, float power);

}  // namespace geow2

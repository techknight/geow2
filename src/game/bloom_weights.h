#pragma once

#include <array>

namespace geow2 {

// The bloom blur weights of sub_82064848 (w0..w7: the centre and each
// pair k texels away) and the level weights of sub_82064F88, as the
// original computes them.
std::array<float, 8> blur_weights(float radius, float precision);
std::array<float, 5> combine_weights(float precision, float falloff);

}  // namespace geow2

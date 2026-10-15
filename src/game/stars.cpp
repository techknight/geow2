#include "game/stars.h"

#include <cmath>

namespace geow2 {
namespace {

constexpr float kTwoPow31 = 4.65661287e-10f;  // dat_820006C8: 2^-31
constexpr float kTwoPow32 = 2.32830644e-10f;  // dat_820006E4: 2^-32

}  // namespace

void StarField::init(GameRng& rng) {
  // sub_82075318: four draws per star, in this order.
  stars.resize(kCount);
  for (Star& s : stars) {
    const float x = std::fmaf(float(rng.next()), kTwoPow31, -1.0f);            // fmsubs
    const float y = std::fmaf(float(rng.next()), kTwoPow31, -1.0f) * 0.5625f;  // dat_82002FF4
    const float r = float(rng.next()) * kTwoPow32;
    const float r2 = r * r;
    s.near = r2 * r;
    const float spread = (2.0f - s.near) * 1800.0f;  // dat_820006FC, dat_82002FFC
    s.depth = (1.25f - s.near) * -3000.0f;           // dat_82000768, dat_82002FF8
    s.x = x * spread;
    s.y = spread * y;
    s.phase = float(rng.next()) * kTwoPow32;
  }
}

void StarField::update(const GameMath& gm, float zoom, std::vector<StarVertex>& out) {
  // sub_82075548. Fade: min(fade + 0.03, 1) (dat_82001168).
  const float f = fade + 0.03f;
  fade = 1.0f - f >= 0.0f ? f : 1.0f;
  // Brightness falls with zoom: (1 / zoom)^1.5 (dat_82001228, f64).
  const float zoom_factor = float(std::pow(double(1.0f / zoom), 1.5));
  constexpr float kTwoPi = 6.28318548f;  // dat_82182698
  out.clear();
  out.reserve(stars.size());
  for (Star& s : stars) {
    // Phase steps down by near * 0.02 + 0.01 and restarts at 1.0.
    const float step = std::fmaf(s.near, 0.02f, 0.01f);   // dat_82001194, dat_820011B0
    const float base = std::fmaf(s.near, 0.8f, 0.01f);    // dat_82000754
    const float p = s.phase - step;
    s.phase = p >= 0.0f ? p : 1.0f;
    const float tw = float(gm.sin(double(s.phase * kTwoPi)));
    const float lift = ((s.near + 1.0f) * tw) * tw;
    const float b = (std::fmaf(lift, 0.065f, base) * zoom_factor) * fade;  // dat_82000D48
    // Colour (4.2, 5.1, 6.0) (dat_82003000, dat_82003004, dat_82001414).
    out.push_back({s.x, s.y, s.depth, b * 4.2f, b * 5.1f, b * 6.0f});
  }
}

}  // namespace geow2

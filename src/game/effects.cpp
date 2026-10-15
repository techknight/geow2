#include "game/effects.h"

#include <cmath>

namespace geow2 {
namespace {

constexpr float kTwoPow31 = 4.65661287e-10f;  // dat_820006C8
constexpr float kTwoPow32 = 2.32830644e-10f;  // dat_820006E4
// The wheel's six colours (0x82180728 red, 0x82180740 green, 0x82180758
// blue).
constexpr float kRed[6] = {9.6f, 9.6f, 0.75f, 0.25f, 0.25f, 9.6f};
constexpr float kGreen[6] = {0.75f, 3.4f, 9.6f, 3.4f, 0.75f, 0.25f};
constexpr float kBlue[6] = {0.25f, 0.25f, 0.25f, 9.6f, 9.6f, 3.4f};
constexpr float kTwoPi = 6.28318548f;  // 0x82180724

}  // namespace

void EffectColours::random(GameRng& rng, float alpha, float out[4]) {
  const uint32_t i = rng.next() % 6;
  out[0] = kRed[i];
  out[1] = kGreen[i];
  out[2] = kBlue[i];
  out[3] = alpha;
}

void Effects::wheel(float t, const float c[4], float out[4]) const {
  // c: the point (r4 in sub_82030678).
  // Six points on a circle (cos * R, sin * R + O) at k * 2pi/6 + phase + t
  // (sin/cos the game's own; sin * R + O fused).
  const float step = kTwoPi * 0.166666672f;  // dat_82000700
  const float R = tuning.wheel_radius, O = tuning.wheel_offset;
  static constexpr float kK[6] = {0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
  float wx[6], wy[6];
  for (int k = 0; k < 6; ++k) {
    const float base = k == 1 ? step : step * kK[k];
    const float angle = float(double(k == 1 ? wheel_phase_ + base : base + wheel_phase_) + t);
    const float s = float(gm_.sin(angle)), co = float(gm_.cos(angle));
    wy[k] = std::fmaf(s, R, O);
    wx[k] = co * R;
  }
  // Inverse squared distances from (x, y, z, w - 1) to each point.
  const float z = c[2], w = c[3] - 1.0f;
  float d[6];
  for (int k = 0; k < 6; ++k) {
    const float ey = float(double(c[1]) - wy[k]), ex = float(double(c[0]) - wx[k]);
    d[k] = 1.0f / (((w * w + z * z) + ey * ey) + ex * ex);
  }
  const float n = 1.0f / (((((d[5] + d[4]) + d[3]) + d[2]) + d[1]) + d[0]);
  for (float& v : d) v = n * v;
  out[0] = kRed[0] * d[0] + kRed[1] * d[1] + kRed[2] * d[2] + kRed[3] * d[3] + kRed[4] * d[4] + kRed[5] * d[5];
  out[1] = kGreen[0] * d[0] + kGreen[1] * d[1] + kGreen[2] * d[2] + kGreen[3] * d[3] + kGreen[4] * d[4] +
           kGreen[5] * d[5];
  out[2] = kBlue[0] * d[0] + kBlue[1] * d[1] + kBlue[2] * d[2] + kBlue[3] * d[3] + kBlue[4] * d[4] + kBlue[5] * d[5];
  out[3] = d[0] + d[1] + d[2] + d[3] + d[4] + d[5];
}

void Effects::random_cube(float s, float out[4]) {
  out[2] = std::fmaf(float(rng_.next()), kTwoPow31, -1.0f) * s;
  out[1] = std::fmaf(float(rng_.next()), kTwoPow31, -1.0f) * s;
  out[0] = std::fmaf(float(rng_.next()), kTwoPow31, -1.0f) * s;
  out[3] = 0.0f;
}

void Effects::random_in_ball(float r, float out[4]) {
  // sub_82030270(1.0) until inside the unit ball; then scaled.
  for (;;) {
    float v[4];
    random_cube(1.0f, v);
    const float x = v[0], y = v[1], z = v[2], w = v[3];
    if (1.0 < double(((w * w + z * z) + x * x) + y * y)) continue;
    out[0] = float(double(x) * r);
    out[1] = float(double(y) * r);
    out[2] = float(double(z) * r);
    out[3] = float(double(w) * r);
    return;
  }
}

void Effects::explosion(float radius, float x, float y, int count) {
  if (particles_.full()) return;
  // Two neighbouring wheel colours at the position, around a random turn.
  const float t = std::fmaf(float(rng_.next()), kTwoPow31, -1.0f) * 0.75f;  // dat_82000718
  const float pos[4] = {x, y, 0.0f, 0.0f};
  float a[4], b[4];
  wheel(float(double(t) - double(0.1f)), pos, a);  // dat_82000714
  wheel(float(double(t) + double(0.1f)), pos, b);
  const float speed = float(double(tuning.size) * radius);
  const int groups = int(float(count) * tuning.factor) / 12;
  float d[4];
  for (int i = 0; i < 4; ++i) d[i] = b[i] * 1.1f - a[i];  // dat_82000710
  auto blend = [&](float out[3]) {
    const float u = float(rng_.next()) * kTwoPow32;
    for (int i = 0; i < 3; ++i) out[i] = d[i] * u + a[i];
  };
  for (int g = 0; g < groups; ++g) {
    float v[4];
    random_in_ball(speed, v);
    float c[3];
    blend(c);
    const float X = v[0], Y = v[1], Z = v[2];
    // The six orderings of the components, then (new colour) negated.
    particles_.add(x, y, 0.0f, X, Y, Z, c);
    particles_.add(x, y, 0.0f, Y, X, Z, c);
    particles_.add(x, y, 0.0f, X, Z, Y, c);
    particles_.add(x, y, 0.0f, Y, Z, X, c);
    particles_.add(x, y, 0.0f, Z, X, Y, c);
    particles_.add(x, y, 0.0f, Z, Y, X, c);
    blend(c);
    particles_.add(x, y, 0.0f, -X, -Y, -Z, c);
    particles_.add(x, y, 0.0f, -Y, -X, -Z, c);
    particles_.add(x, y, 0.0f, -X, -Z, -Y, c);
    particles_.add(x, y, 0.0f, -Y, -Z, -X, c);
    particles_.add(x, y, 0.0f, -Z, -X, -Y, c);
    particles_.add(x, y, 0.0f, -Z, -Y, -X, c);
  }
}

void Effects::trigger(const UnitEffect& e, float x, float y) {
  switch (e.kind) {
    case UnitEffect::kExplosion:
      // sub_82031788: also passes colour0 rgb times its alpha, unused.
      explosion(e.radius, x, y, e.count);
      break;
    case UnitEffect::kRandomExplosion: {
      // sub_82031820: draws a random wheel colour first (passed, unused).
      float c[4];
      EffectColours::random(rng_, 1.0f, c);
      explosion(e.radius, x, y, e.count);
      break;
    }
    default:
      break;
  }
}

}  // namespace geow2

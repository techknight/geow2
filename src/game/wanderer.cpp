#include "game/wanderer.h"

#include <cmath>

#include "game/rotation.h"

namespace geow2 {
namespace {

constexpr float kTwoPow32 = 2.32830644e-10f;  // dat_820006E4
// Unit.Wanderer.Colour (variable 0x821A057C, default (0.5, 0.25, 1, 1)).
constexpr float kColour[4] = {0.5f, 0.25f, 1.0f, 1.0f};

}  // namespace

Wanderer::Wanderer(World& w, float px, float py) {
  // Base (sub_82047030): unit "wanderer", type code 0x12, flags 0x900.
  x = px;
  y = py;
  unit = w.units.find("wanderer");
  type = 0x12;
  flags |= 0x900;
  radius = 7.0f;  // dat_820017B0
  flags |= 0x14;  // fading in, touches players
  sx = sy = 2.5f;  // dat_82001158
  for (int i = 0; i < 4; ++i) colour[i] = kColour[i];
  // Spin: fmadds(r * 2^-32, 0.05, 0.1), reversed (and mirrored) on a
  // second draw above 0.5.
  spin = std::fmaf(float(w.rng.next()) * kTwoPow32, 0.05f, 0.1f);
  if (0.5f < float(w.rng.next()) * kTwoPow32) {
    spin = -spin;
    sx = -sx;
  }
  // Toward the target at 10 (dat_8200070C), plus a random vector of 10.
  constexpr float kSpeed = 10.0f;
  if (const PlayerTarget* t = w.target(*this)) {
    const float dx = t->x - px, dy = t->y - py;
    const float inv = 1.0f / std::sqrt(dy * dy + dx * dx);
    vx = (inv * dx) * kSpeed;
    vy = (inv * dy) * kSpeed;
  }
  const Vec2 r = w.random_vector(kSpeed);
  vx = r.x + vx;
  vy = r.y + vy;
  // sub_82045A88.
  if (!(flags & kEntityCounted)) {
    flags |= kEntityCounted;
    ++w.enemy_count;
  }
}

void Wanderer::update(World& w) {
  for (int i = 0; i < 4; ++i) colour[i] = kColour[i];
  heading = spin + heading;
  if (const PlayerTarget* t = w.target(*this)) {
    const float dx = t->x - x, dy = t->y - y;
    const float inv = 1.0f / std::sqrt(dy * dy + dx * dx);
    vx = vx + (inv * dx) * 0.1f;  // dat_82000714
    vy = vy + (inv * dy) * 0.1f;
  }
  const Vec2 r = w.random_vector(5.0f);  // dat_820006F0
  vx = r.x + vx;
  vy = r.y + vy;
  // Speed eased 5% of the way to 50 (dat_82001040, dat_82000D60).
  const float s = std::sqrt(vy * vy + vx * vx);
  const float k = std::fmaf(50.0f - s, 0.05f, s) / s;
  vx = vx * k;
  vy = vy * k;
  w.separate(*this, radius, 1.0f);
}

void Wanderer::wall(World&, Vec2 n) {
  // Reflect: v -= n * 2 (n . v) (dat_820006FC).
  const float d = n.x * vx + n.y * vy;
  const float k = d * 2.0f;
  vx = vx - n.x * k;
  vy = vy - n.y * k;
}

Mat4 Wanderer::custom_placement(World& w) {
  // Tilted toward the velocity like style TiltVelocity, turned by the
  // heading; mirrored (row 0 negated, dat_82000C88) when the spin is
  // positive.
  const Mat4 basis = look_at_basis({x, y, 0, 1}, {vx, vy, 100.0f, 0}, unit ? unit->up : Vec4{0, 1, 0, 0});
  Mat4 r = rotation_axis(w.gm, heading, 0, 0, 1);
  if (spin > 0.0f)
    for (int c = 0; c < 4; ++c) r.m[c] = r.m[c] * -1.0f;
  return mul(r, basis);
}

}  // namespace geow2

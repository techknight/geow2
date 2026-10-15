#include "game/power_up.h"

#include <cmath>

#include "game/rotation.h"

namespace geow2 {
namespace {

constexpr float kTwoPow32 = 2.32830644e-10f;  // dat_820006E4

}  // namespace

PowerUp::PowerUp(World& w, float px, float py, int k) {
  x = px;
  y = py;
  type = 0x10;
  unit_based = false;
  kind = k;
  age = 0.0f;  // dat_820006CC
  credit = w.units.find("credit");
  flags |= 0x1410;  // fading in, model, 0x1000
  // The port gathers lights through the glow flag; the original lights
  // the item from its draw.
  flags |= kEntityGlow;
  heading = (float(w.rng.next()) * kTwoPow32) * 6.28318548f;  // dat_82181968
  const Vec2 v = w.random_vector(16.0f);  // dat_82001104
  vx = v.x;
  vy = v.y;
  colour[0] = 2.4f;  // dat_82001C7C
  colour[1] = 2.1f;  // dat_82001C78
  colour[2] = 0.0f;
  colour[3] = 3.0f;  // dat_820006F8
  sx = sy = 1.0f;
  radius = 7.5f;  // dat_82001C74
  shape_points = 0x10;  // the table at 0x82181970 (unused: it draws its model)
}

Mat4 PowerUp::tilt(World& w) const {
  const Vec4 up = credit ? credit->up : Vec4{0, 1, 0, 0};
  const Mat4 basis = look_at_basis({x, y, 0, 1}, {vx, vy, 100.0f, 0}, up);  // dat_82001020
  return mul(rotation_axis(w.gm, heading, 0, 0, 1), basis);
}

void PowerUp::update(World& w) {
  age = w.step + age;
  if (60.0f <= age) {  // dat_82000CE0
    // Line debris at its centre with no impact speed, then removal.
    if (credit) {
      Killer k;
      k.x = x;
      k.y = y;
      w.shatter(credit, tilt(w), x, y, radius, k, colour);
    }
    w.remove(*this);
  }
  if (1024.0f < vy * vy + vx * vx) {  // dat_82001C70
    vx = vx * 0.985000014f;  // dat_82001BB8
    vy = vy * 0.985000014f;
  }
  // Turn 5 a second (dat_820006F0), wrapped to [-pi, pi] (fctid rounds
  // to nearest).
  const double d = double(std::fmaf(w.step, 5.0f, heading)) * 0.15915494309189535;  // dat_82000D00
  heading = float((d - std::nearbyint(d)) * 6.2831853072);  // dat_82000CF8
}

void PowerUp::wall(World&, Vec2 n) {
  const float d = (n.x * vx + n.y * vy) * 2.0f;  // dat_820006FC
  vx = vx - n.x * d;
  vy = vy - n.y * d;
}

void PowerUp::light(World& w, std::vector<LightVertex>& out) {
  const float p[4] = {x, y, 0.0f, 1.0f};
  w.effects.wheel(heading + 3.14159274f, p, colour);  // dat_82001C64
  emit_light(out, 35.0f, x, y, colour);  // dat_82001C6C
}

void PowerUp::draw(World& w, LineBatch& out) {
  if (!credit || !credit->has_model) return;
  const Mat4 m = tilt(w);
  if (!(flags & kEntityFadingIn)) {
    draw_model_part(out, credit->model, 0, m, colour);
    return;
  }
  for (int i = 0; i < 2; ++i) {
    const float g = float(std::fmod(double(fade * 3.0f), 1.0)) + float(i);  // sub_8212FC00
    float f = g;
    if (f >= 1.0f) f = float(2.0 - double(f));
    if (f < 0.025f) continue;  // dat_82000D64
    const float s = std::fmaf(g, 1.4f, 0.1f);  // dat_82000FF8, dat_82000714
    Mat4 scaled = m;
    for (int r = 0; r < 3; ++r)
      for (int c = 0; c < 4; ++c) scaled.m[r * 4 + c] = m.m[r * 4 + c] * s;
    const float ghost[4] = {colour[0], colour[1], colour[2], colour[3] * f};
    draw_model_part(out, credit->model, 0, scaled, ghost);
  }
}

void PowerUp::collected(World& w, const PlayerTarget& p) {
  vx = 0.0f;
  vy = 0.0f;
  if (credit) {
    Killer k;
    k.x = p.x;
    k.y = p.y;
    k.vx = p.vx;
    k.vy = p.vy;
    w.shatter(credit, tilt(w), x, y, radius, k, colour);
  }
  w.remove(*this);
}

}  // namespace geow2

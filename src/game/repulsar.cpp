#include "game/repulsar.h"

#include <cmath>

namespace geow2 {
namespace {

constexpr float kTwoPow32 = 2.32830644e-10f;  // dat_820006E4
constexpr float kTwoPow31 = 4.65661287e-10f;  // dat_820006C8
constexpr float kPi = 3.14159274f;            // dat_8200203C
constexpr float kLeadSpeed = 288.0f;          // dat_82002054

// The heading wrap (player.md): in double, minus the nearest integer of
// a / 2pi (fctid), times 2pi, rounded once.
float wrap(float a) {
  const double t = double(a) * 0.15915494309137826;  // dat_82000D00
  return float((t - std::nearbyint(t)) * 6.2831853072);  // dat_82000CF8
}

// Two fsel: below `lo` gives lo, then at or above `hi` gives hi.
float clamp_sel(float v, float lo, float hi) {
  const float a = lo - v >= 0.0f ? lo : v;
  return v - hi >= 0.0f ? hi : a;
}

float angle_to(const World& w, float x, float y, float tx, float ty) {
  return float(w.gm.atan2(ty - y, tx - x));
}

}  // namespace

Vec2 lead_point(float speed, float k, Vec2 a, Vec2 av, Vec2 b, Vec2 bv) {
  const float rx = av.x + -bv.x, ry = av.y + -bv.y;
  const float dx = b.x - a.x, dy = b.y - a.y;
  const float r2 = ry * ry + rx * rx;
  const float d2 = dy * dy + dx * dx;
  const float bb = dy * ry + dx * rx;
  const float q = std::fmaf(-speed, speed, r2);
  const float disc = std::fmaf(bb, bb, -(d2 * q));
  if (!(disc >= 0.0f)) return b;
  const float r = std::sqrt(disc);
  const float inv = 1.0f / q;
  float t1 = (-bb - r) * inv, t2 = (r - bb) * inv;
  if (t1 < 0.0f && t2 < 0.0f) return b;
  t1 = -t1 >= 0.0f ? t1 : 0.0f;
  t2 = -t2 >= 0.0f ? t2 : 0.0f;
  const float t = t2 - t1 >= 0.0f ? t1 : t2;
  const float u = t * k;
  return {b.x + rx * u, b.y + ry * u};
}

Repulsar::Repulsar(World& w, float px, float py) {
  // Base (sub_82047030): unit "repulsar", type code 0xB, flags 0x900. The
  // spawner's angle is not used.
  x = px;
  y = py;
  unit = w.units.find("repulsar");
  type = 0xB;
  flags |= 0x900;
  flags |= 0x14;  // fading in, shootable
  colour[0] = 1.0f;
  colour[1] = 0.0f;
  colour[2] = 0.0f;
  colour[3] = 1.0f;
  radius = 7.0f;  // dat_820017B0
  heading = std::fmaf(float(w.rng.next()), kTwoPow31, -1.0f) * kPi;
  timer = 0.0f;
  state = 0;
  aim_time = std::fmaf(float(w.rng.next()), kTwoPow32, 1.0f) * 0.4f;  // dat_82001164
  flash = 0.0f;
  dizzy = 0.0f;
  // (Its 40-point shape table is only drawn without models.) sub_82045A88:
  if (!(flags & kEntityCounted)) {
    flags |= kEntityCounted;
    ++w.enemy_count;
  }
}

void Repulsar::update(World& w) {
  const PlayerTarget* t = w.target(*this);
  const float dt = w.step;
  {
    const float f = flash - 0.3f;  // dat_82000708, per update
    flash = f >= 0.0f ? f : 0.0f;
    const float d = dizzy - dt;
    dizzy = d >= 0.0f ? d : 0.0f;
  }
  if (state == 0) {
    if (t) {
      const float d = wrap(angle_to(w, x, y, t->x, t->y) - heading);
      const float turn = clamp_sel(d * 0.5f, -0.15f, 0.15f);  // dat_82000724, dat_82002068, dat_82000D58
      heading = turn + heading;
      if (std::fabs(d) < 0.2f) {  // dat_82000D5C
        timer = timer + dt;
        if (timer > aim_time) {
          // Charge (sound event 0x14).
          timer = 0.0f;
          state = 1;
        }
      } else {
        const float r = timer - dt;
        timer = r >= 0.0f ? r : 0.0f;
      }
    }
  } else if (state == 2) {
    timer = timer + dt;
    if (timer > 1.0f) {
      timer = 0.0f;
      state = 0;
      aim_time = w.random_float(0.4f) + 0.4f;
    }
  }
  const float s = w.gm.sinf(heading), c = w.gm.cosf(heading);
  if (state == 1 && t) {
    const Vec2 p = lead_point(kLeadSpeed, 1.0f, {x, y}, {vx, vy}, {t->x, t->y}, {t->vx, t->vy});
    const float d = wrap(angle_to(w, x, y, p.x, p.y) - heading);
    if (dizzy <= 0.0f) heading = clamp_sel(d, -0.01f, 0.01f) + heading;  // dat_82002050, dat_820011B0
    if (std::fabs(d) > 1.0f) {
      timer = timer + dt;
      if (timer > 0.5f) {  // dat_82000724
        timer = 0.0f;
        state = 2;
      }
    } else {
      const float r = timer - dt;
      timer = r >= 0.0f ? r : 0.0f;
    }
    vx = vx + c * kRepulsarSpeed;
    vy = vy + s * kRepulsarSpeed;
  }
  vx = vx * 0.9f;  // 0x82000D70
  vy = vy * 0.9f;
  w.separate(*this, 7.0f, 1.0f);
}

void Repulsar::wall(World& w, Vec2 n) {
  if (state == 1) {
    const float s = w.gm.sinf(heading), c = w.gm.cosf(heading);
    if (n.x * c + n.y * s < -0.75f) {  // dat_82000C8C
      timer = 0.0f;
      state = 2;
    }
  }
  const float k = (vx * n.x + n.y * vy) * 2.0f;
  vx = vx - n.x * k;
  vy = vy - n.y * k;
}

void Repulsar::hit_by_shot(World& w, const Killer& k) {
  if (k.kind != 1) {
    kill(w, k);
    return;
  }
  // The shield: only a shot within 3pi/8 of the rear, moving against it,
  // kills.
  const float a = angle_to(w, x, y, k.x, k.y);
  const float back = heading + kPi;
  const float d = wrap(back - a);
  bool killed = false;
  if (std::fabs(d) < 1.17809725f) {  // dat_8200206C
    const float sb = w.gm.sinf(back), cb = w.gm.cosf(back);
    const float inv = 1.0f / std::sqrt(k.vy * k.vy + k.vx * k.vx);
    const float dot = (inv * k.vy) * sb + (inv * k.vx) * cb;
    killed = dot < 0.0f;
  }
  if (killed) {
    kill(w, k);
    return;
  }
  // Deflected: shoved by the shot's velocity bent toward its
  // perpendicular, dizzy, turned a little away from the lead point.
  const float dx = x - k.x, dy = y - k.y;
  const float inv1 = 1.0f / std::sqrt(dy * dy + dx * dx);
  const float inv2 = 1.0f / std::sqrt(k.vy * k.vy + k.vx * k.vx);
  const float ux = inv1 * dx, uy = inv1 * dy, wx = inv2 * k.vx, wy = inv2 * k.vy;
  const float cross = -ux * wy + uy * wx;
  const float g = cross > 0.0f ? -1.0f : 1.0f;  // dat_82000C88
  const float h = std::fabs(cross) * 0.5f;
  vx = vx + (k.vx + (k.vy * g - k.vx) * h) * kRepulsarShove;
  vy = (k.vy + (-k.vx * g - k.vy) * h) * kRepulsarShove + vy;
  dizzy = kRepulsarDizzyTime;
  if (const PlayerTarget* t = w.target(*this)) {
    const Vec2 p = lead_point(kLeadSpeed, 1.0f, {x, y}, {vx, vy}, {t->x, t->y}, {t->vx, t->vy});
    const float dl = wrap(angle_to(w, x, y, p.x, p.y) - heading);
    const float f = dl >= 0.0f ? -kRepulsarDizzyFactor : kRepulsarDizzyFactor;
    heading = f / std::fmaf(std::fabs(dl), kRepulsarDizzyScale, 1.0f) + heading;
  }
  flash = 1.0f;
  // (Sound event 7, at most once per 0x23 ticks.)
}

void Repulsar::kill(World& w, const Killer& k) {
  // An extra explosion of 400 (radius 8, dat_82000D18), then the unit's
  // death.
  w.effects.explosion(8.0f, x, y, 400);
  die(w, k, 0);
}

void Repulsar::draw(World& w, LineBatch& out) {
  if (!unit || !unit->has_model) return;
  const Mat4 m = placement(w);
  if (!(flags & kEntityFadingIn)) {
    // The body flashes four times as bright, the core twice.
    const float c0 = std::fmaf(flash, 3.0f, 1.0f), c1 = flash + 1.0f;
    const float body[4] = {c0, c0, c0, 1.0f}, core[4] = {c1, c1, c1, 1.0f};
    draw_model_part(out, unit->model, 0, m, body);
    if (unit->model.parts.size() > 1) draw_model_part(out, unit->model, 1, m, core);
    return;
  }
  for (int i = 0; i < 2; ++i) {
    const float g = float(std::fmod(double(fade * 3.0f), 1.0)) + float(i);  // sub_8212FC00
    float a = g;
    if (a >= 1.0f) a = 2.0f - a;
    if (!(a >= 0.025f)) continue;  // dat_82000D64
    const float q = std::fmaf(g, 1.4f, 0.1f);  // dat_82000FF8, dat_82000714
    Mat4 scaled = m;
    for (int r = 0; r < 3; ++r)
      for (int c = 0; c < 4; ++c) scaled.m[r * 4 + c] = m.m[r * 4 + c] * q;
    const float ghost[4] = {1.0f, 1.0f, 1.0f, a};
    draw_model_part(out, unit->model, 0, scaled, ghost);
    if (unit->model.parts.size() > 1) draw_model_part(out, unit->model, 1, scaled, ghost);
  }
}

}  // namespace geow2

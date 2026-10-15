#include "game/player.h"

#include <cmath>

#include "game/world.h"

namespace geow2 {
namespace {

// dat_821A1D64: the steps per second (60).
constexpr int kStepsPerSecond = 60;

// int(60 * t) mod 10, as fctiwz and the compiler's division give it.
int blink_phase(float t) { return int(float(kStepsPerSecond) * t) % 10; }

}  // namespace

ModeRules mode_rules(int mode) {
  ModeRules r;
  r.mode = mode;
  if (mode == 0) return r;
  // From the reset: lives and bombs 3, no rule flags, no time limit.
  r.lives_mode = 0;
  r.start_lives = 3;
  r.start_bombs = 3;
  r.flags = 0;
  r.length = 0.0f;
  if (mode == 2) r.level_time = 30.0f;  // dat_82000CDC
  if (mode >= 3) {
    r.start_lives = 0;
    r.start_bombs = 0;
    r.flags |= 3;
  }
  return r;
}

float trigger_axis(uint8_t raw) { return float(raw) * 0.00392156886f; }  // dat_82000E18

void player_spawned(PlayerLife& p) {
  p.present = true;
  p.removed = false;
  p.fading = true;
  p.fade = 0.0f;
  p.shield = 3.33f;  // dat_82001E94
  p.bomb_wait = 0.0f;
  p.shield_shown = true;
  p.bomb_latch = false;
}

bool player_timers(PlayerLife& p, float step) {
  bool blink = false;
  if (0.0f < p.shield) {
    p.shield = p.shield - step;
    // 0 < t < 1 and int(60 t) mod 10 == 4: ShieldOff.
    if (0.0f < p.shield && p.shield < 1.0f && blink_phase(p.shield) == 4) blink = true;
  }
  if (0.0f < p.bomb_wait) p.bomb_wait = p.bomb_wait - step;
  // Shown while t > 0, except over the last second when int(60 t) mod 10
  // is above 4 (dat_820006C4).
  const float t = p.shield;
  if (t <= 0.0f)
    p.shield_shown = false;
  else if (t <= 1.0f && blink_phase(t) > 4)
    p.shield_shown = false;
  else
    p.shield_shown = true;
  if (0 < p.wall_wait) --p.wall_wait;
  return blink;
}

int player_bomb(PlayerLife& p, float trigger, const ModeRules& rules) {
  if (!(0 < rules.start_bombs || (rules.flags & 2) == 0)) return 0;
  if (trigger <= 0.15f) {  // dat_82000D58
    p.bomb_latch = false;
    return 0;
  }
  if (p.bomb_latch) return 0;
  p.bomb_latch = true;
  if (!(p.bomb_wait <= 0.0f)) return 0;
  p.bomb_wait = 0.2f;  // dat_82000D5C
  if (p.bombs <= 0) return 2;  // BombDryFire
  --p.bombs;
  p.bomb_used = true;
  return 1;
}

void player_exhaust(World& w, const PlayerMotion& p) {
  constexpr float kParticleThrust = 0.5f;  // Unit.Player.ParticleThrust (0x8219F5C8)
  constexpr float kTwoPow32 = 2.32830644e-10f;  // dat_820006E4
  const float dt = w.step;
  const float speed_dt = std::sqrt(p.vy * p.vy + p.vx * p.vx) * dt;
  const float s = w.gm.sinf(p.heading), c = w.gm.cosf(p.heading);
  const float k = speed_dt * kParticleThrust;
  const float back_y = k * -s, back_x = k * -c, back_z = k * 0.0f;
  const float z0 = dt * 0.0f;
  // From 7 behind the ship (dat_820017B0), moved by this step's velocity.
  const float ex = (p.x + -c * 7.0f) + p.vx * dt;
  const float ey = (p.y + -s * 7.0f) + p.vy * dt;
  // Single player: the wheel at the ship (turned by pi, dat_82001E80) and
  // 1.1 times it (dat_82000710).
  const float at[4] = {p.x, p.y, 0.0f, 1.0f};
  float b[4];
  w.effects.wheel(3.14159274f, at, b);
  const float a[4] = {b[0] * 1.1f, b[1] * 1.1f, b[2] * 1.1f, b[3] * 1.1f};
  const float cube = speed_dt * 0.1f;  // dat_82000714
  // One particle: colour lo + (hi - lo) * u, spread along the trail by i,
  // velocity (back + kick) * v.
  auto emit = [&](int i, float v, const float lo[4], const float d[4]) {
    const float fi = float(i);
    const float along = fi * 0.5f;  // dat_82000724
    const float u = float(w.rng.next()) * kTwoPow32;
    const float col[3] = {d[0] * u + lo[0], d[1] * u + lo[1], d[2] * u + lo[2]};
    float r[4];
    w.effects.random_cube(cube, r);
    w.particles.add(along * back_x + ex, back_y * along + ey, back_z * along + z0, (back_x + r[0]) * v,
                    (r[1] + back_y) * v, (r[2] + back_z) * v, col);
  };
  if (p.boost_time > 0.0f) {
    // Boosted: from (8, 8, 8, 1) toward the brighter colour, faster.
    const float lo[4] = {8.0f, 8.0f, 8.0f, 1.0f};  // dat_82000D18
    const float d[4] = {a[0] - 8.0f, a[1] - 8.0f, a[2] - 8.0f, a[3] - 1.0f};
    for (int i = 0; i < 4; ++i) emit(i, std::fmaf(float(i), 0.1f, 1.0f) * 2.0f, lo, d);
  }
  if (p.slow_time > 0.0f) {
    // Slowed: from the wheel colour toward white, at three quarters.
    const float d[4] = {1.0f - b[0], 1.0f - b[1], 1.0f - b[2], 1.0f - b[3]};
    for (int i = 0; i < 4; ++i) emit(i, std::fmaf(float(i), 0.1f, 1.0f) * 0.75f, b, d);  // dat_82000718
  } else {
    const float d[4] = {a[0] - b[0], a[1] - b[1], a[2] - b[2], a[3] - b[3]};
    for (int i = 0; i < 4; ++i) emit(i, std::fmaf(float(i), 0.1f, 1.0f), b, d);
  }
}

}  // namespace geow2

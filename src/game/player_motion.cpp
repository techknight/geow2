#include "game/player_motion.h"

#include <cmath>

namespace geow2 {
namespace {

constexpr float kDeadZone = 0.25f;          // dat_82000720
constexpr float kMoveThreshold = 0.2f;      // dat_82000D5C (squared length)
constexpr float kThrust = 50.0f;            // dat_82001040
constexpr float kBoostThrust = 70.0f;       // dat_820018EC
constexpr float kSlowFactor = 0.7f;         // dat_82001190
constexpr float kDamping = 0.8f;            // dat_82000754
constexpr float kTurnRate = 0.1f;           // dat_82000714
constexpr double kInvTwoPi = 0.15915494309137826;  // dat_82000D00 (f64)
constexpr double kTwoPi = 6.2831853072;            // dat_82000CF8 (f64)
constexpr double kCornerScale = 0.7070000171661377;  // dat_820027A8 (f64)
constexpr double kCornerTest = 1.5;                  // dat_82001228 (f64)

double clamp01(double v) {
  // The game's clamp pattern: max(v, 0.0) then min(v, 1.0), compared as
  // in the decompile (sub_8204EE28, sub_8204FF00).
  double lo = -v < 0.0 ? v : 0.0;
  return float(v - 1.0) < 0.0f ? lo : 1.0;
}

}  // namespace

float stick_axis(int16_t raw, bool is_y) {
  // (raw - 0.5) * (+/- 1/32767.5); constants 0x38000080 / 0xB8000080.
  const float scale = is_y ? -3.05180438e-05f : 3.05180438e-05f;
  return (float(raw) - 0.5f) * scale;
}

Vec2 stick_vector(float x, float y) {
  // fsqrts and fdivs: single precision (sub_8204EE28 disassembly).
  const float m = std::sqrt(x * x + y * y);
  const float inv = 1.0f / m;
  const float clamped = float(clamp01(double(m)));
  if (clamped < kDeadZone) return {0.0f, 0.0f};
  return {(inv * x) * clamped, (inv * y) * clamped};
}

void player_thrust(PlayerMotion& p, Vec2 move, const GameMath& gm) {
  const float mx2 = float(double(move.x) * move.x), my2 = float(double(move.y) * move.y);
  const float len2 = my2 + mx2;
  if (len2 <= kMoveThreshold) {
    p.thrust_level = 0.0f;
  } else {
    double accel = p.boost_time > 0.0f ? kBoostThrust : kThrust;
    if (p.slow_time > 0.0f) accel = double(float(accel * double(kSlowFactor)));
    p.vx = float(accel * move.x) + p.vx;
    p.vy = float(accel * move.y) + p.vy;
    p.thrust_level = float(clamp01(double(std::sqrt(len2))));  // fsqrts
  }
  p.vy = p.vy * kDamping;
  p.vx = p.vx * kDamping;
  if (kMoveThreshold < len2) {
    // Heading eases toward the movement direction by a tenth of the
    // difference. The wrap converts with fctid (round to nearest in the
    // default mode, not truncation), so the turn is the shortest one; the
    // final step is a fused single-precision multiply-add (fmadds at
    // 0x82050454).
    const double d = double(float(gm.atan2(move.y, move.x)) - p.heading) * kInvTwoPi;  // frsp, fsubs
    const double wrapped = d - std::nearbyint(d);
    p.heading = std::fmaf(float(wrapped * kTwoPi), kTurnRate, p.heading);
  }
}

void integrate(PlayerMotion& p, float step) {
  const float dy = p.vy * step;
  p.x = p.x + p.vx * step;
  p.y = dy + p.y;
}

bool arena_contact(float radius, float half_x, float half_y, float& x, float& y, Vec2& normal) {
  float hy = float(double(half_y) - radius), hx = float(double(half_x) - radius);
  const double top = double(y + hy), right = double(hx - x), left = double(x + hx);
  double margin = double(hy - y);
  if (top < 0.0) margin = top;
  if (right < 0.0) margin = right;
  if (left < 0.0) margin = left;
  if (0.0 <= margin) return false;

  double nx = 0.0, ny = 0.0;
  if (right < 0.0) nx = -1.0;
  if (double(hy - y) < 0.0) ny = -1.0;
  if (left < 0.0) nx = 1.0;
  if (top < 0.0) ny = 1.0;
  float cy = (-hy - y < 0.0f) ? y : -hy;
  float cx = (-hx - x < 0.0f) ? x : -hx;
  if (y - hy < 0.0f) hy = cy;
  if (x - hx < 0.0f) hx = cx;
  y = hy;
  x = hx;
  const double scale = std::fma(ny, ny, nx * nx) - kCornerTest < 0.0 ? 1.0 : kCornerScale;  // fmadd
  normal = {float(scale * nx), float(scale * ny)};
  return true;
}

bool player_wall(PlayerMotion& p, Vec2 n) {
  const float along = p.vx * n.y + p.vy * -n.x;
  p.vx = along * n.y;
  p.vy = along * -n.x;
  const bool sound = p.wall_sound_frames < 1;
  p.wall_sound_frames = 2;
  return sound;
}

}  // namespace geow2

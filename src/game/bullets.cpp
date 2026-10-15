#include "game/bullets.h"

#include <cmath>
#include <cstring>
#include <stdexcept>

namespace geow2 {
namespace {

constexpr float kFireThreshold = 0.001f;    // dat_82001A18 (cooldown at or below: can fire)
constexpr float kFireStickMin = 0.2f;       // dat_82000D5C (squared length)
constexpr float kSpread1 = 0.03f;           // dat_82001168
constexpr float kSpread2 = 0.06f;           // dat_82001154
constexpr float kSpread3 = 0.09f;           // dat_82001150
constexpr double kSideSpeed = 1.3;          // dat_82001A00 (f32 widened)
constexpr double kCentreSpeed = 1.5;        // dat_82000C90
constexpr float kBulletSpeed = 400.0f;      // dat_82000F88
constexpr float kCooldown = 0.1f;           // dat_82000714
constexpr float kUpgradedCooldown = 0.0666666701f;  // dat_82001E84

// sub_8204E748 + sub_82052DB8 for player bullet types 0 / 2: speed is a
// single-precision factor * 400; the velocity adds the shooter's
// (fmadds), then the heading is atan2 of the result.
Bullet make_bullet(const PlayerMotion& p, float angle, double factor, int type, const GameMath& gm) {
  Bullet b;
  b.type = type;
  if (type == 2) b.gravity = 1.25f;
  b.x = p.x;
  b.y = p.y;
  const float speed = float(float(factor) * kBulletSpeed);
  b.vx = std::fmaf(speed, gm.cosf(angle), p.vx);
  b.vy = std::fmaf(speed, gm.sinf(angle), p.vy);
  b.heading = float(gm.atan2(b.vy, b.vx));
  return b;
}

}  // namespace

std::vector<Vec2> read_shape(std::span<const uint8_t> image, uint32_t address, int count) {
  constexpr uint32_t kBase = 0x82000000;
  std::vector<Vec2> points;
  for (int i = 0; i < count; ++i) {
    float xy[2];
    for (int k = 0; k < 2; ++k) {
      const size_t o = address - kBase + size_t(i) * 8 + size_t(k) * 4;
      if (o + 4 > image.size()) throw std::runtime_error("shape table outside the image");
      const uint32_t bits = (uint32_t(image[o]) << 24) | (uint32_t(image[o + 1]) << 16) | (uint32_t(image[o + 2]) << 8) |
                            image[o + 3];
      std::memcpy(&xy[k], &bits, 4);
    }
    points.push_back({xy[0], xy[1]});
  }
  return points;
}

void bullet_colour(int colour, float out[4]) {
  static constexpr float kColours[5][4] = {{1.0f, 0.75f, 0.1f, 1.0f},
                                           {1.0f, 0.25f, 0.1f, 1.0f},
                                           {0.1f, 0.25f, 1.0f, 1.0f},
                                           {1.0f, 1.0f, 0.1f, 1.0f},
                                           {0.1f, 1.0f, 0.25f, 1.0f}};
  const int k = colour >= 1 && colour <= 4 ? colour : 0;
  for (int i = 0; i < 4; ++i) out[i] = kColours[k][i];
}

BulletShape BulletShape::from_image(std::span<const uint8_t> image) { return {read_shape(image, 0x82181D4C, 6)}; }

void player_fire_step(const PlayerMotion& p, PlayerWeapon& w, Vec2 fire, float step, const GameMath& gm,
                      bool may_fire, std::vector<Bullet>& out) {
  // sub_8204FF00: fire only when the cooldown is down and the fire stick
  // is pushed far enough; otherwise count the cooldown down.
  if (w.cooldown <= kFireThreshold) {
    // sub_8204EFD0 returns at once, the cooldown untouched, when the mode
    // forbids firing.
    if (kFireStickMin < fire.x * fire.x + fire.y * fire.y && may_fire) {
      const float a = float(gm.atan2(fire.y, fire.x));
      out.push_back(make_bullet(p, a - kSpread1, kSideSpeed, 0, gm));
      out.push_back(make_bullet(p, a, kCentreSpeed, 2, gm));
      out.push_back(make_bullet(p, a + kSpread1, kSideSpeed, 0, gm));
      if (w.upgrade_time > 0.0f) {
        out.push_back(make_bullet(p, a - kSpread2, kCentreSpeed, 2, gm));
        out.push_back(make_bullet(p, a + kSpread2, kCentreSpeed, 2, gm));
        out.push_back(make_bullet(p, a - kSpread3, kSideSpeed, 0, gm));
        out.push_back(make_bullet(p, a + kSpread3, kSideSpeed, 0, gm));
        w.cooldown = w.cooldown + kUpgradedCooldown;
      } else {
        w.cooldown = w.cooldown + kCooldown;
      }
    }
  } else {
    w.cooldown = w.cooldown - step;
  }
}

bool bullet_step(Bullet& b, float step, float half_x, float half_y, Effects& fx) {
  // Update (sub_82052418): a bullet that hit a wall last step is removed
  // now with a spark: sub_82030C58(1.5, &pos, 20, ...) (dat_82000C90).
  if (b.removed) return false;
  if (b.hit) {
    fx.explosion(1.5f, b.x, b.y, 20);
    return false;
  }
  b.prev_x = b.x;
  b.prev_y = b.y;
  // Common update: position += velocity * step.
  const float dy = b.vy * step;
  b.x = b.x + b.vx * step;
  b.y = dy + b.y;
  // Arena contact (sub_8205C5D8) → the bullet's wall handler (sub_82051AF0):
  // first contact marks it (sound event 0x23, not ported yet).
  float x = b.x, y = b.y;
  Vec2 n;
  if (arena_contact(b.radius, half_x, half_y, x, y, n)) {
    b.x = x;
    b.y = y;
    b.hit = true;
  }
  return true;
}

std::vector<Segment> shape_segments(std::span<const Vec2> points, float x, float y, float heading, float sx, float sy,
                                    const GameMath& gm) {
  // Rotate and scale each point, translate; fused as the original
  // (0x820619B8..0x82061A08, and 0x82061AE0..0x82061B2C alike).
  std::vector<Segment> out;
  const float s = gm.sinf(heading), c = gm.cosf(heading);
  const float sx_sin = sx * s, sy_sin = sy * s, sx_cos = sx * c, sy_cos = sy * c;
  for (size_t i = 0; i + 1 < points.size(); i += 2) {
    const Vec2 p0 = points[i], p1 = points[i + 1];
    const float x1 = std::fmaf(-p1.y, sy_sin, std::fmaf(p1.x, sx_cos, x));
    const float y1 = std::fmaf(p1.y, sy_cos, p1.x * sx_sin) + y;
    const float x0 = std::fmaf(-p0.y, sy_sin, std::fmaf(p0.x, sx_cos, x));
    const float y0 = std::fmaf(p0.x, sx_sin, p0.y * sy_cos) + y;
    out.push_back({{x0, y0, 0.0f, 0.0f}, {x1, y1, 0.0f, 0.0f}});
  }
  return out;
}

std::vector<Segment> bullet_segments(const Bullet& b, const BulletShape& shape, const GameMath& gm) {
  // sub_82061908.
  return shape_segments(shape.points, b.x, b.y, b.heading, b.sx, b.sy, gm);
}

}  // namespace geow2

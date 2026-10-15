#include "game/snake.h"

#include <cmath>

#include "game/geoms.h"
#include "game/rotation.h"

namespace geow2 {
namespace {

constexpr float kTwoPow32 = 2.32830644e-10f;  // dat_820006E4
constexpr float kTwoPi = 6.28318548f;         // dat_82182028

// Unit.SnakeHead.Colour (0x821A007C) and Unit.SnakeBody.Colour (0x821A0038).
constexpr float kHeadColour[4] = {0.00999999978f, 0.00999999978f, 3.0f, 1.0f};
constexpr float kBodyColour[4] = {1.0f, 0.300000012f, 0.00999999978f, 0.75f};

constexpr int kSegments = 23;

// Per segment index (0 = the tail tip): radius factor, length and
// brightness; 1.0 from index 6 on (the head constructor's switch).
struct SegmentShape {
  float radius, length, brightness;
};
constexpr SegmentShape kTaper[6] = {
    {0.400000006f, 0.0469999984f, 0.180000007f},  // dat_82001164, dat_820023E4, dat_820023E8
    {0.5f, 0.135000005f, 0.266000003f},           // dat_82000724, dat_820023EC, dat_820023F0
    {0.600000024f, 0.254000008f, 0.363000005f},   // dat_82001160, dat_820023F4, dat_820023F8
    {0.699999988f, 0.388000011f, 0.495999992f},   // dat_82001190, dat_820023FC, dat_82002400
    {0.800000012f, 0.589999974f, 0.676999986f},   // dat_82000754, dat_82002418, dat_82002414
    {0.899999976f, 0.898000002f, 0.925000012f},   // dat_82000D70, dat_82002410, dat_8200240C
};

float wrap_angle(float a) {
  const double t = double(a) * 0.15915494309137826;  // dat_82000D00
  return float((t - std::nearbyint(t)) * 6.2831853072);  // fctid, dat_82000CF8
}

}  // namespace

Entity* snake_head_of(Entity& e) {
  if (e.type == 0xD) return &e;
  if (e.type == 0xE) return static_cast<SnakeSegment&>(e).head;
  return nullptr;
}

// ---------------------------------------------------------------------------
// Head

SnakeHead::SnakeHead(World& w, float px, float py) {
  x = px;
  y = py;
  unit = w.units.find("snakehead");
  type = 0xD;
  flags |= 0x900;
  flags |= 0x14;
  for (int i = 0; i < 4; ++i) colour[i] = kHeadColour[i];
  heading = (float(w.rng.next()) * kTwoPow32) * kTwoPi;
  radius = 4.5f;  // dat_820018F0
  sx = sy = 1.25f;  // dat_82000768
  phase = (float(w.rng.next()) * kTwoPow32) * kTwoPi;
  shape_points = 0x1C;  // 0x82182030
}

void SnakeHead::build_segments(World& w) {
  SnakeSegment* last = nullptr;
  for (segments = 0; segments < kSegments; ++segments) {
    const int i = segments;
    const SegmentShape s = i < 6 ? kTaper[i] : SegmentShape{1.0f, 1.0f, 1.0f};
    // Detached, the tail tip lives longest: (23 - i) / 23 * 0.27 s.
    const float delay = (float(kSegments - i) * 0.0434782617f) * 0.270000011f;  // dat_82002408, dat_82002404
    auto seg = std::make_unique<SnakeSegment>(w, *this, last, i, s.length, s.brightness, delay, s.radius);
    if (i & 1) seg->flags |= 0x100000;
    last = static_cast<SnakeSegment*>(w.add(std::move(seg)));
    first = last;
  }
  // sub_82045A88.
  if (!(flags & kEntityCounted)) {
    flags |= kEntityCounted;
    ++w.enemy_count;
  }
}

void SnakeHead::update(World& w) {
  for (int i = 0; i < 4; ++i) colour[i] = kHeadColour[i];
  phase = phase + w.step;
  const float s = w.gm.sinf(heading), c = w.gm.cosf(heading);
  vy = vy + s * 36.0f;  // dat_820020AC
  vx = vx + c * 36.0f;
  vx = vx * 0.8f;  // dat_82000754
  vy = vy * 0.8f;
  // Steering toward the target, off by a wiggle cos(p) + 1.75 cos(4p).
  const float wiggle = std::fmaf(w.gm.cosf(phase * 4.0f), 1.75f, w.gm.cosf(phase));  // dat_820006F4, dat_82002458
  if (const PlayerTarget* t = w.target(*this)) {
    const float a = float(w.gm.atan2(t->y - y, t->x - x));
    const float d = a - (heading + wiggle);
    heading = std::fmaf(wrap_angle(d), 0.1f, heading);  // dat_82000714
  }
}

void SnakeHead::wall(World&, Vec2 n) {
  const float d = n.x * vx + n.y * vy;
  const float k = d * 2.0f;  // dat_820006FC
  vx = vx - n.x * k;
  vy = vy - n.y * k;
}

void SnakeHead::detach_all(bool d) {
  for (SnakeSegment* s = first; s; s = s->behind) {
    s->head = nullptr;
    s->flags |= 0x40;  // no longer touches players
    s->drop = d;
  }
  first = nullptr;
}

void SnakeHead::kill(World& w, const Killer& k) {
  if (flags & kEntityRemoved) return;
  // The segments let go, dropping geoms later unless the kill was kind 0
  // or 2; then the usual death; sound event 0x4E.
  detach_all(!(k.kind == 0 || k.kind == 2));
  w.play(audio::Event::kSnakeExplode);
  Entity::kill(w, k);
}

void SnakeHead::on_delete(World&) { detach_all(false); }

// ---------------------------------------------------------------------------
// Segment

SnakeSegment::SnakeSegment(World& w, SnakeHead& h, SnakeSegment* b, int i, float length, float bright, float d,
                           float radius_factor) {
  x = h.x;
  y = h.y;
  unit = w.units.find("snakebody");
  type = 0xE;
  flags |= 0x900;
  delay = d;
  flash = 0.0f;
  drop = false;
  index = i;
  ahead = nullptr;
  behind = b;
  if (b) b->ahead = this;
  flags |= 0x42014;
  heading = h.heading;
  brightness = bright;
  head = &h;
  colour[0] = kBodyColour[0] * bright;
  colour[1] = kBodyColour[1] * bright;
  colour[2] = kBodyColour[2] * bright;
  colour[3] = kBodyColour[3];
  shape_points = 6;  // 0x82181FF4
  sx = 0.5f;          // dat_82000724
  sy = length * 0.5f;
  const float r = radius_factor * 2.75f;  // dat_82002388
  radius = r - 0.75f >= 0.0f ? r : 0.75f;  // dat_82000718
}

void SnakeSegment::update(World& w) {
  // The colour flashes toward (5, 4, 3, 1) by flash^3, fading in 5 steps.
  const float h = flash;
  const float h3 = (h * h) * h;
  const float r = (5.0f - kBodyColour[0]) * h3 + kBodyColour[0];  // dat_820006F0
  const float g = (4.0f - kBodyColour[1]) * h3 + kBodyColour[1];  // dat_820006F4
  const float b = (3.0f - kBodyColour[2]) * h3 + kBodyColour[2];  // dat_820006F8
  const float a = (1.0f - kBodyColour[3]) * h3 + kBodyColour[3];
  colour[3] = a;
  colour[0] = r * brightness;
  colour[1] = g * brightness;
  colour[2] = b * brightness;
  flash = h - 0.2f >= 0.0f ? h - 0.2f : 0.0f;  // dat_82000D5C
  if (head) {
    // Following the one ahead (the head for the first): pulled harder the
    // farther it is (distance / 8 clamped to 0.5 .. 2, times 36), damped,
    // and matching a fifth of its velocity. The original takes the
    // distance and its reciprocal from frsqrte / fres (DIFFERENCES.md).
    const Entity& lead = ahead ? static_cast<const Entity&>(*ahead) : static_cast<const Entity&>(*head);
    const float dx = lead.x - x, dy = lead.y - y;
    const float q = (dy * dy + dx * dx) + 9.99999975e-06f;  // dat_82002378
    const float inv = float(1.0 / std::sqrt(double(q)));
    const float dist = float(std::sqrt(double(q)));
    const float nx = inv * dx, ny = dy * inv;
    if (dist > 1.0f) {
      double k = double(dist * 0.125f);  // dat_82001204
      k = k - 0.5 >= 0.0 ? k : 0.5;      // dat_82002370 (f64)
      k = k - 2.0 >= 0.0 ? 2.0 : k;      // dat_82001180 (f64)
      const float pull = float(k) * 36.0f;
      vx = vx + pull * nx;
      vy = vy + ny * pull;
      vx = vx * 0.8f;
      vy = vy * 0.8f;
      vx = vx + (lead.vx - vx) * 0.2f;
      vy = vy + (lead.vy - vy) * 0.2f;
      heading = float(w.gm.atan2(ny, nx));
    } else {
      heading = lead.heading;
    }
  } else {
    // Let go of: dies when its delay runs out (kind 3 when it drops geoms).
    delay = delay - w.step;
    if (delay <= 0.0f) {
      Killer k;
      k.x = x;
      k.y = y;
      k.vx = vx;
      k.vy = vy;
      k.kind = drop ? 3 : 0;
      k.object_type = type;
      k.source = this;
      kill(w, k);
    }
    vx = vx * 0.8f;
    vy = vy * 0.8f;
  }
}

void SnakeSegment::draw(World& w, LineBatch& out) { draw_unit(w, out, colour); }

void SnakeSegment::wall(World&, Vec2 n) {
  // Slides along the wall.
  const float k = vx * n.y + vy * -n.x;
  vx = k * n.y;
  vy = k * -n.x;
}

Mat4 SnakeSegment::custom_placement(World& w) {
  // Turned by the heading and moved, then the model's y and z scaled by
  // the segment's length (sy * 2).
  Mat4 m = rotation_axis(w.gm, heading, 0, 0, 1);
  m.m[12] = m.m[12] + x;
  m.m[13] = m.m[13] + y;
  const float l = sy * 2.0f;
  Mat4 s = Mat4::identity();
  for (int c = 0; c < 4; ++c) {
    s.m[4 + c] = s.m[4 + c] * l;
    s.m[8 + c] = s.m[8 + c] * l;
  }
  return mul(s, m);
}

void SnakeSegment::hit_by_shot(World& w, const Killer& k) {
  // A shot only flashes it (sound event 0x4F); anything else kills it.
  if (k.kind == 1) {
    flash = 1.0f;
    w.play(audio::Event::kSnakeHitTail);
    return;
  }
  kill(w, k);
}

void SnakeSegment::kill(World& w, const Killer& k) {
  if (flags & kEntityRemoved) return;
  // Attached: the head dies with the same killer.
  if (head && !(head->flags & kEntityRemoved)) head->kill(w, k);
  w.effects.explosion(4.0f, x, y, 40);  // dat_820006F4
  if (drop) drop_geoms(w, *this, k);
  // Debris from where the killer is (0.9 of the radius toward it), plus
  // a random offset, flung 7.5 times as hard when it died by itself.
  if (unit && unit->has_model && !unit->model.parts.empty()) {
    float px = k.x, py = k.y;
    const float dx = px - x, dy = py - y;
    const float len = std::sqrt(dy * dy + dx * dx);
    const float inv = 1.0f / len;
    if (len > 0.1f) {  // dat_82000714
      px = ((inv * dx) * radius) * 0.9f + x;
      py = y + ((inv * dy) * radius) * 0.9f;
    }
    const float strength = k.source == this ? 7.5f : 1.0f;  // dat_82001C74
    const Vec2 off = w.random_vector(1.0f);
    px = px + off.x;
    py = py + off.y;
    w.debris.smash(w.effects, w.rng, strength, unit->model.parts[0], placement(w), px, py, k.vx, k.vy, colour);
  }
  w.remove(*this);
}

void SnakeSegment::on_delete(World&) {
  if (head && head->first) head->detach_all(false);
  if (ahead) ahead->behind = nullptr;
  if (behind) behind->ahead = nullptr;
}

}  // namespace geow2

#include "game/gate.h"

#include <algorithm>
#include <cmath>

#include "game/rotation.h"
#include "game/shockwave.h"

namespace geow2 {
namespace {

constexpr float kTwoPow32 = 2.32830644e-10f;  // dat_820006E4

// Unit.Gate tuning variables (defaults; values at 0x8219E874 .. 0x8219EA44).
constexpr float kRotationSpeed = 2.0f;  // Unit.Gate.RotationSpeed
constexpr float kDistance = 65.0f;      // Unit.Gate.Distance (dat_82001948)
constexpr float kDamp = 0.9f;           // Unit.Gate.Damp (dat_82000D70)
constexpr float kWobble = 1.0f;         // Unit.Gate.Wobble
constexpr float kWobbleDamp = 0.975f;   // Unit.Gate.WobbleDamp (dat_82001984)
constexpr float kChainTime = 1.0f;      // Unit.Gate.ChainTime
// Unit.Gate.Colour, .BarColour, .FlashColour (0x8219E958, 0x8219E99C,
// 0x8219E9E0).
constexpr float kColour[4] = {1.5f, 0.5f, 0.00999999978f, 1.0f};
constexpr float kBarColour[4] = {0.560000002f, 0.970000029f, 2.8499999f, 1.0f};
constexpr float kFlashColour[4] = {3.0f, 4.0f, 5.0f, 1.0f};

// The flashes fade by 0.2 a step (dat_82000D5C; fsel).
float fade_flash(float v) { return v - 0.2f >= 0.0f ? v - 0.2f : 0.0f; }

}  // namespace

GateEnd::GateEnd(World& w, float px, float py) {
  x = px;
  y = py;
  unit = w.units.find("gate");
  type = 0x18;
  flags |= 0x900;
  radius = 3.0f;  // dat_820006F8
  flags |= 0x414;
  sx = sy = 4.5f;  // dat_820018F0
  heading = 0.0f;
  for (int i = 0; i < 4; ++i) colour[i] = kColour[i];
  if (float(w.rng.next()) * kTwoPow32 > 0.5f) sign = -1.0f;
  prev_x = x;
  prev_y = y;
  shape_points = 0x10;  // 0x821816D0
  if (!(flags & kEntityCounted)) {
    flags |= kEntityCounted;
    ++w.enemy_count;
  }
}

Entity* spawn_gate(World& w, float px, float py) {
  // Two random points 4 around the spawn point give the direction; the
  // ends sit Distance apart about their middle.
  const Vec2 v1 = w.random_vector(4.0f);
  const float p1x = px + v1.x, p1y = v1.y + py;
  const Vec2 v2 = w.random_vector(4.0f);
  const float qx = v2.x + px, qy = v2.y + py;
  const float mx = (qx + p1x) * 0.5f, my = (p1y + qy) * 0.5f;
  const float dx = p1x - mx, dy = p1y - my;
  const float len = std::sqrt(dy * dy + dx * dx);
  float ux = 1.0f, uy = 0.0f;
  if (!(len < 0.0001f)) {  // dat_820010FC
    const float inv = 1.0f / len;
    ux = inv * dx;
    uy = inv * dy;
  }
  const float hx = (kDistance * 0.5f) * ux, hy = (uy * kDistance) * 0.5f;
  auto* a = static_cast<GateEnd*>(w.add(std::make_unique<GateEnd>(w, mx - hx, my - hy)));
  auto* b = static_cast<GateEnd*>(w.add(std::make_unique<GateEnd>(w, hx + mx, hy + my)));
  a->partner = b;
  b->partner = a;
  a->leader = true;
  w.play(audio::Event::kGateSpawn);
  return a;
}

void GateEnd::update(World& w) {
  prev_x = x;
  prev_y = y;
  beam_flash = fade_flash(beam_flash);
  flash = fade_flash(flash);
  if (!partner) {
    Killer k;
    k.x = x;
    k.y = y;
    k.vx = vx;
    k.vy = vy;
    k.object_type = type;
    k.source = this;
    kill(w, k);
  } else {
    GateEnd& p = *partner;
    const float dx = p.x - x, dy = p.y - y;
    const float len = std::sqrt(dy * dy + dx * dx);
    const float inv = 1.0f / len;
    const float ux = inv * dx, uy = inv * dy;
    if (len > 0.1f) {  // dat_82000714
      if (leader) {
        // A spring toward the gate's length, clamped to +/- the length.
        const float e = len - kDistance;
        float k = -kDistance - e >= 0.0f ? -kDistance : e;
        k = e - kDistance >= 0.0f ? kDistance : k;
        vx = vx + ux * k;
        vy = vy + uy * k;
        p.vx = p.vx - ux * k;
        p.vy = p.vy - uy * k;
      }
      // Both ends turn the pair the leader's way.
      const float s = kRotationSpeed * (leader ? sign : p.sign);
      vx = vx + uy * s;
      vy = vy + (-ux) * s;
    }
    // A wobble the leader draws and both share.
    if (leader) {
      const Vec2 r = w.random_vector(kWobble);
      wobble_x = r.x + wobble_x;
      wobble_y = r.y + wobble_y;
      wobble_x = wobble_x * kWobbleDamp;
      wobble_y = wobble_y * kWobbleDamp;
      vx = vx + wobble_x;
      vy = wobble_y + vy;
    } else {
      vx = p.wobble_x + vx;
      vy = p.wobble_y + vy;
    }
  }
  vx = vx * kDamp;
  vy = vy * kDamp;
  // The colour flashes toward (4, 3, 2, 1) by flash^3 (used without a model).
  const float f3 = (flash * flash) * flash;
  colour[0] = (4.0f - kColour[0]) * f3 + kColour[0];
  colour[1] = (3.0f - kColour[1]) * f3 + kColour[1];
  colour[2] = (2.0f - kColour[2]) * f3 + kColour[2];
  colour[3] = (1.0f - kColour[3]) * f3 + kColour[3];
  w.separate(*this, radius, 1.0f);
}

void GateEnd::wall(World&, Vec2 n) {
  const float k = (n.x * vx + n.y * vy) * 2.0f;  // dat_820006FC
  vy = vy - n.y * k;
  vx = vx - n.x * k;
  const float kw = (n.x * wobble_x + n.y * wobble_y) * 2.0f;
  wobble_y = wobble_y - n.y * kw;
  wobble_x = wobble_x - n.x * kw;
}

Mat4 GateEnd::custom_placement(World& w) {
  if (!partner) return placement_style_none(w.gm, x, y, heading);
  // Facing the other end.
  const Mat4 basis = look_at_basis({x, y, 0, 1}, {partner->x - x, partner->y - y, 0, 1}, unit ? unit->up : Vec4{0, 1, 0, 0});
  return mul(rotation_axis(w.gm, heading, 0, 0, 1), basis);
}

void GateEnd::draw(World& w, LineBatch& out) {
  // The end: white, flashing (1 + 2 flash).
  const float b = std::fmaf(flash, 2.0f, 1.0f);
  const float tint[4] = {b, b, b, 1.0f};
  draw_unit(w, out, tint);
  // The beam (sub_82047AA8, the leader's): Unit.Gate.BarColour toward
  // FlashColour by the beam's flash cubed, one line quad.
  if (leader && partner && !(flags & kEntityRemoved)) {
    const float f3 = (beam_flash * beam_flash) * beam_flash;
    float c[4];
    for (int i = 0; i < 4; ++i) c[i] = (kFlashColour[i] - kBarColour[i]) * f3 + kBarColour[i];
    emit_line(out, kLineWidthUnit, 1.0f, {x, y, 0, 0}, {partner->x, partner->y, 0, 0}, c);
  }
}

void GateEnd::hit_by_shot(World& w, const Killer& k) {
  // A shot only flashes it.
  if (k.kind == 1) {
    flash = 1.0f;
    return;
  }
  kill(w, k);
}

void GateEnd::kill(World& w, const Killer& k) {
  if (flags & kEntityRemoved) return;
  if (k.kind == 5) {
    // Set off by a player: its killed effect, then the score and geoms
    // (sub_82046010 flags 5).
    if (unit) w.effects.trigger(unit->killed, x, y);
    die(w, k, 5);
    return;
  }
  // Otherwise the score, geoms and sound 2 (flags 1), white debris, and
  // the other end dies with the same killer.
  die(w, k, 1);
  const float white[4] = {1, 1, 1, 1};
  w.shatter(unit, placement(w), x, y, radius, k, white);
  if (partner) partner->kill(w, k);
}

void GateEnd::on_delete(World&) {
  if (partner) partner->partner = nullptr;
}

int GateEnd::special_points(const World& w) const {
  // The chain count squared, times 100 (at most 5).
  const int chain = w.players.empty() ? 0 : w.players[w.scorer].gate_chain;
  const int c = chain < 1 ? 1 : std::min(chain, 5);
  return c * c * 100;
}

void gate_crossings(World& w) {
  for (size_t gi = 0; gi < w.entities.size(); ++gi) {
    Entity& e = *w.entities[gi];
    if (e.type != 0x18 || (e.flags & kEntityRemoved)) continue;
    auto& a = static_cast<GateEnd&>(e);
    if (!a.leader || !a.partner) continue;
    GateEnd& b = *a.partner;
    const float ex = b.x - a.x, ey = b.y - a.y;
    const float len = std::sqrt(ey * ey + ex * ex);
    if (len < 1.0f) continue;
    const float inv = 1.0f / len;
    const float ux = inv * ex, uy = inv * ey;
    const float nx = uy, ny = -ux;
    const float epx = b.prev_x - a.prev_x, epy = b.prev_y - a.prev_y;
    for (size_t pi = 0; pi < w.players.size(); ++pi) {
      PlayerTarget& p = w.players[pi];
      if (p.removed) continue;
      // Which side the player was on (against last step's beam) and is
      // on, 6 either side of the line (dat_82001414).
      const bool s0 = (p.prev_y - a.prev_y) * -epx + (p.prev_x - a.prev_x) * epy >= 0.0f;
      const float rx = p.x - a.x, ry = p.y - a.y;
      const bool s1 = (ry - ny * 6.0f) * -ux + uy * (rx - nx * 6.0f) >= 0.0f;
      const bool s2 = (ry + ny * 6.0f) * -ux + uy * (rx + nx * 6.0f) >= 0.0f;
      if (s0 == s1 && s0 == s2) continue;
      if (!(ry * uy + rx * ux >= 0.0f)) continue;
      if (!((p.y - b.y) * uy + (p.x - b.x) * ux <= 0.0f)) continue;
      // Two blasts (70, over a sixth of 70 / step a second, kind 1), the
      // chain counted, both ends destroyed (kind 5).
      const float speed = float(70.0f / w.step) * 0.166666672f;  // dat_820018EC, dat_82000700
      static_cast<Shockwave*>(w.add(std::make_unique<Shockwave>(w, 70.0f, speed, a.x, a.y, 1)))->player = pi;
      static_cast<Shockwave*>(w.add(std::make_unique<Shockwave>(w, 70.0f, speed, b.x, b.y, 1)))->player = pi;
      if (w.clock - p.gate_chain_time > kChainTime || p.gate_chain >= 5) p.gate_chain = 0;
      ++p.gate_chain;
      p.gate_chain_time = w.clock;
      w.play(audio::Event(uint8_t(audio::Event::kGateScore1) + uint8_t(std::min(p.gate_chain, 5) - 1)));
      Killer k;
      k.x = p.x;
      k.y = p.y;
      k.vx = p.vx;
      k.vy = p.vy;
      k.kind = 5;
      k.object_type = 1;  // the player
      k.player = pi;
      a.kill(w, k);
      b.kill(w, k);
      break;
    }
  }
}

void bounce_off_beams(World& w, Bullet& s) {
  const float mx = s.x - s.prev_x, my = s.y - s.prev_y;
  for (size_t gi = 0; gi < w.entities.size(); ++gi) {
    Entity& e = *w.entities[gi];
    if (e.type != 0x18 || (e.flags & 0x11)) continue;
    auto& a = static_cast<GateEnd&>(e);
    if (!a.leader || !a.partner) continue;
    const GateEnd& b = *a.partner;
    const float epx = b.prev_x - a.prev_x, epy = b.prev_y - a.prev_y;
    const float ex = b.x - a.x, ey = b.y - a.y;
    if (epy * epy + epx * epx < 1.0f || ey * ey + ex * ex < 1.0f) continue;
    const float s0 = (s.prev_y - a.prev_y) * -epx + epy * (s.prev_x - a.prev_x);
    const float s1 = (s.y - a.y) * -ex + ey * (s.x - a.x);
    if (!((s0 < 0.0f && s1 >= 0.0f) || (s0 >= 0.0f && s1 < 0.0f))) continue;
    const float t = std::fmaf(s.prev_x - a.x, my, (a.y - s.prev_y) * mx) / std::fmaf(my, ex, -(ey * mx));
    if (!(t >= 0.0f && t <= 1.0f)) continue;
    // Off the beam's normal (on the side it came from), 2 clear of it.
    const float g = 1.0f / std::sqrt(ey * ey + ex * ex);  // frsqrte in the original
    float nx = ey * g, ny = (-ex) * g;
    if (s0 < 0.0f) {
      nx = nx * -1.0f;
      ny = ny * -1.0f;
    }
    s.x = (ex * t + a.x) + nx * 2.0f;
    s.y = (ey * t + a.y) + ny * 2.0f;
    const float k = (s.vx * nx + s.vy * ny) * 2.0f;
    s.vx = s.vx - k * nx;
    s.vy = s.vy - ny * k;
    s.heading = float(w.gm.atan2(s.vy, s.vx));
    a.beam_flash = 1.0f;
    s.hit = false;
    ++s.bounces;
    w.play(audio::Event::kGateBounceBullet1);  // the pair alternates behind its gate
    return;
  }
}

}  // namespace geow2

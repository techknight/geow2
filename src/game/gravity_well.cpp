#include "game/gravity_well.h"

#include <cmath>

#include "game/enemies.h"
#include "game/geoms.h"
#include "game/rotation.h"
#include "game/shockwave.h"

namespace geow2 {
namespace {

constexpr float kTwoPow32 = 2.32830644e-10f;  // dat_820006E4
constexpr float kTwoPow31 = 4.65661287e-10f;  // dat_820006C8
constexpr float kPi = 3.14159274f;            // dat_820019FC
constexpr float kTwoPi = 6.28318548f;         // dat_82181750
constexpr float kReach = 256.0f;              // dat_82001A04

// fsel minimum: b when a - b >= 0, else a.
float min_sel(float a, float b) { return a - b >= 0.0f ? b : a; }

}  // namespace

GravityWell::GravityWell(World& w, float px, float py) {
  x = px;
  y = py;
  unit = w.units.find("gravitywell");
  type = 3;
  flags |= 0x900;
  flags |= 0x1002034;
  hum = 0;
  active = false;
  ui = false;
  hits = kWellStartHits;
  heading = std::fmaf(float(w.rng.next()), kTwoPow31, -1.0f) * kPi;
  const float p[4] = {x, y, 0.0f, 1.0f};
  w.effects.wheel(heading + kPi, p, colour);
  phase = kTwoPi * (float(w.rng.next()) * kTwoPow32);
  const float size = float(hits) + 5.0f;  // dat_820006F0
  radius = sx = sy = size;
  shape_points = 0x30;  // the ring at 0x8219EBA8
  if (!(flags & kEntityCounted)) {
    flags |= kEntityCounted;
    ++w.enemy_count;
  }
}

void GravityWell::update(World& w) {
  shoved = false;
  swallowed = false;
  if (burst) {
    w.remove(*this);
    return;
  }
  if (active) {
    pull(w);
    heading = std::fmaf(float(hits), 0.01f, heading) + 0.02f;  // dat_820011B0, dat_82001194
  }
  phase = std::fmaf(float(hits), 0.075f, phase) + 0.05f;  // dat_82000D54, dat_82000D60
  const float c = w.gm.cosf(phase) * 0.1f;
  pulse = w.gm.cosf(phase * 0.1f) * 0.3f;  // dat_82000708
  if (hits == kWellMaxHits) {
    // The warning every 30 updates at the limit.
    if (!ui) {
      if (hum % 30 == 0) w.play(audio::Event::kGravityWellWarning);
      ++hum;
    }
  } else {
    hum = 0;
  }
  if (hits > kWellMaxHits) {
    // Overloaded: gone at the next update, releasing 15 protons in rings
    // of 7, 5 and 3 at 150, 120 and 90 (sound event 0x11).
    burst = true;
    swallowed = true;
    flags |= 0x40;
    w.play(audio::Event::kGravityWellExplode);
    if (!ui) {
      static constexpr struct {
        int count;
        float share, speed;
      } kRings[3] = {{7, 0.142857149f, 150.0f}, {5, 0.2f, 120.0f}, {3, 0.333333343f, 90.0f}};  // dat_82001A1C, dat_8200113C ...
      for (const auto& ring : kRings)
        for (int i = 0; i < ring.count; ++i)
          w.add(std::make_unique<Proton>(w, (float(i) * kTwoPi) * ring.share, ring.speed, x, y));
    }
  } else if (!ui && active && w.well_hum) {
    // The hum (sub_82078610).
    w.well_hum(this, hits);
  }
  // Drifting toward its target.
  if (const PlayerTarget* t = w.target(*this)) {
    const float dx = t->x - x, dy = t->y - y;
    const float len = std::sqrt(dy * dy + dx * dx);
    const float inv = 1.0f / len;
    if (len > 0.001f) {  // dat_82001A18
      vx = vx + (inv * dx) * 0.6f;  // dat_82001160
      vy = (inv * dy) * 0.6f + vy;
    }
  }
  vx = vx * 0.95f;  // dat_82001174
  vy = vy * 0.95f;
  // Its size: hits + 5, pulsing by 2 when active (dat_82000CE8).
  const float size = (active ? std::fmaf(c, 20.0f, float(hits)) : float(hits)) + 5.0f;
  radius = sx = sy = size;
  w.separate(*this, radius, 1.0f);
}

void GravityWell::pull(World& w) {
  auto pull_one = [&](float ox, float oy, float& ovx, float& ovy, int kind_type, uint32_t oflags) {
    const float dx = x - ox, dy = y - oy;
    const float len = std::sqrt(dy * dy + dx * dx);
    const float inv = 1.0f / len;
    float ux = inv * dx, uy = inv * dy;
    float k, s = 0.0f;
    bool swirl = true;
    if (oflags & 0x2) {
      k = min_sel(inv * 16.0f, 0.5f);  // dat_82001104
      swirl = false;
    } else if (oflags & 0x20) {
      const float k0 = min_sel(inv * 16.0f, 0.5f);
      s = k0 * 0.9f;  // dat_82000D70
      k = k0 * 0.25f;  // dat_82000720
    } else if (kind_type == 0xE) {
      k = min_sel(inv * 16.0f, 0.175f);  // dat_82001A10
      swirl = false;
    } else {
      const float d2 = len * len;
      if (kind_type == 0xD)
        k = min_sel(500.0f / d2, 0.5f);  // dat_82001170
      else if (kind_type == 0x13)
        k = min_sel(400.0f / d2, 0.5f);  // dat_82000F88
      else
        k = min_sel(1500.0f / d2, 0.75f);  // dat_82001A14, dat_82000718
      s = k * 0.4f;  // dat_82001164
    }
    if (swirl) {
      const float sx2 = (uy - ux) * s + ux, sy2 = (-ux - uy) * s + uy;
      const float m = 1.0f / std::sqrt(sy2 * sy2 + sx2 * sx2);
      ux = m * sx2;
      uy = m * sy2;
    }
    ovx = (ux * k) * 60.0f + ovx;  // dat_82000CE0
    ovy = ovy + (uy * k) * 60.0f;
  };
  w.for_each_near(x, y, kReach, [&](Entity& o) {
    if (&o == this || (o.flags & 0x1011)) return;
    const float dx = x - o.x, dy = y - o.y;
    const float len = std::sqrt(dy * dy + dx * dx);
    if (len > kReach) return;
    if (len < radius) {
      if ((o.flags & 0x2004) == 4) {
        // Swallowed (kind 2: no score, no geoms).
        if (hits <= kWellMaxHits) {
          Killer k;
          k.x = x;
          k.y = y;
          k.vx = vx;
          k.vy = vy;
          k.kind = 2;
          k.object_type = type;
          k.source = this;
          o.kill(w, k);
          ++absorbed;
          swallowed = true;
          ++hits;
        }
        return;
      }
      if (len < 1.0f) return;
    }
    pull_one(o.x, o.y, o.vx, o.vy, o.type, o.flags);
  });
  // The players are units of the hash too (flag 2).
  for (PlayerTarget& p : w.players) {
    if (p.removed || p.fading) continue;
    const float dx = x - p.x, dy = y - p.y;
    const float len = std::sqrt(dy * dy + dx * dx);
    if (len > kReach || (len < radius && len < 1.0f)) continue;
    pull_one(p.x, p.y, p.push_vx, p.push_vy, 1, 0x2);
  }
  // Shots are bent across their path, by their gravity factor.
  if (w.shots) {
    for (Bullet& b : *w.shots) {
      if (b.removed) continue;
      const float dx = x - b.x, dy = y - b.y;
      const float len = std::sqrt(dy * dy + dx * dx);
      const float inv = 1.0f / len;
      const float ux = inv * dx, uy = inv * dy;
      if (len < 1.0f || len > kReach) continue;
      float px = uy, py = -ux;
      const float h = 1.0f / std::sqrt(b.vy * b.vy + b.vx * b.vx);
      if ((h * b.vy) * -ux + uy * (h * b.vx) < 0.0f) {
        px = px * -1.0f;
        py = py * -1.0f;
      }
      const float k = min_sel(inv * 64.0f, 0.5f) * 15.0f;  // dat_82001A0C, dat_82000D24
      b.vx = (px * k) * b.gravity + b.vx;
      b.vy = (py * k) * b.gravity + b.vy;
    }
  }
}

void GravityWell::shove(World& w) {
  // Everything within 256 is pushed away, up to 60 (128 / distance).
  auto push = [&](float ox, float oy, float& ovx, float& ovy) {
    const float dx = x - ox, dy = y - oy;
    const float len = std::sqrt(dx * dx + dy * dy);
    const float inv = 1.0f / len;
    if (len > kReach) return;
    const float k = min_sel(inv * 128.0f, 1.0f) * 60.0f;  // dat_82001A08
    ovx = ovx - (inv * dx) * k;
    ovy = ovy - (inv * dy) * k;
  };
  for (auto& e : w.entities)
    if (e.get() != this && !(e->flags & 0x1081) && e->type != 0xE) push(e->x, e->y, e->vx, e->vy);
  for (PlayerTarget& p : w.players)
    if (!p.removed) push(p.x, p.y, p.push_vx, p.push_vy);
  if (w.shots)
    for (Bullet& b : *w.shots)
      if (!b.removed) push(b.x, b.y, b.vx, b.vy);
  shoved = true;
}

void GravityWell::hit_by_shot(World& w, const Killer& k) {
  // Shot (or reached by another well's blast): shoves, wakes, loses a hit;
  // survives with a burst of 400 (sound event 0xF), or dies.
  shove(w);
  active = true;
  --hits;
  if (hits > 0) {
    w.effects.explosion(8.0f, x, y, 400);  // dat_82000D18
    w.play(audio::Event::kGravityWellHit);
    return;
  }
  kill(w, k);
}

void GravityWell::kill(World& w, const Killer& k) {
  if (flags & kEntityRemoved) return;
  w.award(*this, k);
  if (w.geom_shapes) drop_geoms(w, *this, k);
  const float c[4] = {colour[0] * 0.35f, colour[1] * 0.35f, colour[2] * 0.35f, colour[3] * 0.35f};  // dat_82000704
  w.shatter(unit, placement(w), x, y, radius, k, c);
  w.remove(*this);
  if (!ui) w.play(audio::Event::kGravityWellDie);
  // Brought to 0 by a shot or a blast: its own blast
  // (125, kind 2), which sets off the wells it reaches.
  if (hits <= 0 && (k.kind == 1 || k.kind == 3)) {
    const float speed = float(125.0f / w.step) * 0.0833333358f;  // dat_82001A20, dat_82001528
    static_cast<Shockwave*>(w.add(std::make_unique<Shockwave>(w, 125.0f, speed, x, y, 2)))->player = k.player;
  }
}

int GravityWell::special_points(const World&) const {
  static constexpr int kPoints[12] = {100, 300, 500, 700, 900, 1100, 1500, 1900, 2400, 2900, 3700, 4500};
  return absorbed >= 0 && absorbed < 12 ? kPoints[absorbed] : 6000;
}

int GravityWell::special_geoms() const {
  const int c = absorbed < 0 ? 0 : absorbed > 12 ? 12 : absorbed;
  return c + c * 2 + 5;
}

void GravityWell::light(World& w, std::vector<LightVertex>& out) {
  // Its colour: the wheel at its position, turned by its heading; humming
  // at the maximum, brightened by |sin(hum / 5)| * 4.
  const float p[4] = {x, y, 0.0f, 1.0f};
  w.effects.wheel(heading + kPi, p, colour);
  if (hits == kWellMaxHits) {
    const float g = std::fabs(w.gm.sinf(float(hum) * 0.2f)) * 4.0f;  // dat_82000D5C, dat_820006F4
    for (float& c : colour) c = c + g;
  }
  const float size = (active ? kWellAngryLight : kWellIdleLight) * radius;
  emit_light(out, size, x, y, colour);
}

void GravityWell::draw(World& w, LineBatch& out) {
  if (!unit || !unit->has_model) return;
  const Mat4 m = placement(w);
  const float c[4] = {colour[0] * 0.35f, colour[1] * 0.35f, colour[2] * 0.35f, colour[3] * 0.35f};
  if (!(flags & kEntityFadingIn)) {
    draw_model_part(out, unit->model, 0, m, c);
    return;
  }
  for (int i = 0; i < 2; ++i) {
    const float g = float(std::fmod(double(fade * 3.0f), 1.0)) + float(i);
    float f = g;
    if (f >= 1.0f) f = float(2.0 - double(f));
    if (f < 0.025f) continue;
    // The size from the unfolded value (sub_820490A0).
    const float q = std::fmaf(g, 1.4f, 0.1f);  // dat_82000FF8, dat_82000714
    Mat4 scaled = m;
    for (int r = 0; r < 3; ++r)
      for (int col = 0; col < 4; ++col) scaled.m[r * 4 + col] = m.m[r * 4 + col] * q;
    const float ghost[4] = {c[0], c[1], c[2], f};
    draw_model_part(out, unit->model, 0, scaled, ghost);
  }
}

void GravityWell::wall(World&, Vec2 n) {
  const float k = (n.x * vx + n.y * vy) * 2.0f;  // dat_820006FC
  vx = vx - n.x * k;
  vy = vy - n.y * k;
}

Mat4 GravityWell::custom_placement(World& w) {
  Mat4 m = rotation_axis(w.gm, heading, 0, 0, 1);
  for (int c = 0; c < 4; ++c) {
    m.m[c] = m.m[c] * sx;
    m.m[4 + c] = m.m[4 + c] * sy;
  }
  m.m[12] = m.m[12] + x;
  m.m[13] = m.m[13] + y;
  return m;
}

void well_grid_lights(const World& w, std::vector<GridLight>& out) {
  for (const auto& e : w.entities) {
    if (e->type != 3 || (e->flags & kEntityRemoved)) continue;
    const auto& g = static_cast<const GravityWell&>(*e);
    if (!g.active || g.ui || out.size() >= 32) continue;
    if (g.shoved)
      out.push_back({g.x, g.y, 16.0f, -0.78125f});  // dat_82001104, dat_82002758
    else
      out.push_back({g.x, g.y, 4.0f, (g.pulse + 1.0f) * 0.5f});
  }
}

void well_particle_forces(World& w) {
  w.attractors.clear();
  w.repulsors.clear();
  for (const auto& e : w.entities) {
    if (e->type != 3 || (e->flags & kEntityRemoved)) continue;
    const auto& g = static_cast<const GravityWell&>(*e);
    if (!g.active) continue;
    if (g.shoved || g.swallowed) {
      if (w.repulsors.size() < 16) w.repulsors.push_back({g.x, g.y, 0.0f, g.shoved ? 10.0f : 6.0f});  // dat_8200070C, dat_82001414
    } else if (w.attractors.size() < 16) {
      w.attractors.push_back({g.x, g.y, 0.0f});
    }
  }
}

}  // namespace geow2

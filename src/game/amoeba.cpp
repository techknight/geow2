#include "game/amoeba.h"

#include <cmath>

#include "game/rotation.h"
#include "game/fused.h"

namespace geow2 {
namespace {

constexpr float kTwoPow32 = 2.32830644e-10f;  // dat_820006E4
constexpr float kTwoPi = 6.28318548f;         // dat_8218157C

// sub_82045A88: counted as an enemy.
void count_enemy(World& w, Entity& e) {
  if (!(e.flags & kEntityCounted)) {
    e.flags |= kEntityCounted;
    ++w.enemy_count;
  }
}

void reflect(Entity& e, Vec2 n) {
  const float k = (n.x * e.vx + n.y * e.vy) * 2.0f;
  e.vx = e.vx - n.x * k;
  e.vy = e.vy - n.y * k;
}

// A cell's size: between the low and high radius with the cosine of its
// age times the rate.
float cell_radius(const World& w, float age) {
  const float c = w.gm.cosf(age * kCytoplasmPulsateRate);
  return fmadds((kCytoplasmRadiusHigh - kCytoplasmRadiusLow) * (c + 1.0f), 0.5f, kCytoplasmRadiusLow);
}

}  // namespace

Nucleus::Nucleus(World& w, float px, float py) {
  // Base (sub_82047030): unit "amoebanucleus", type code 0x14, flags 0x900.
  // The spawner's angle is not used.
  x = px;
  y = py;
  unit = w.units.find("amoebanucleus");
  type = 0x14;
  flags |= 0x900;
  bud_timer = 0.0f;
  // Fading in, shootable, and 0x4000: deleting it clears its cells' link.
  flags |= 0x4014;
  cells = 0;
  colour[0] = 0.1f;  // dat_82000714
  colour[1] = 0.4f;  // dat_82001164
  colour[2] = 1.0f;
  colour[3] = 1.0f;
  radius = 5.0f;  // dat_820006F0
  count_enemy(w, *this);
}

void Nucleus::bud(World& w, float spread, bool fade_in) {
  if (cells >= kMaxCytoplasms) return;
  const Vec2 off = w.random_vector(spread);
  auto* c = static_cast<Cytoplasm*>(w.add(std::make_unique<Cytoplasm>(w, x + off.x, y + off.y, this)));
  if (!fade_in) c->flags &= ~uint32_t(kEntityFadingIn);
}

void Nucleus::start(World& w) {
  // The first cells, within 25 (dat_82000D6C): they fade in with it.
  for (int i = 0; i < kCytoplasmAtStart; ++i) {
    if (cells >= kMaxCytoplasms) break;
    bud(w, 25.0f, true);
  }
}

void Nucleus::update(World& w) {
  bud_timer = w.step + bud_timer;
  if (bud_timer >= kCytoplasmSpawnTime) {
    // New cells within 5 (dat_820006F0), deadly at once.
    bud_timer = 0.0f;
    for (int i = 0; i < kCytoplasmPerSpawn; ++i) {
      if (cells >= kMaxCytoplasms) break;
      bud(w, 5.0f, false);
    }
  }
  // A random walk (dat_8200070C), its speed eased toward 30 (dat_82000CDC,
  // dat_82000D60).
  const Vec2 r = w.random_vector(10.0f);
  vx = vx + r.x;
  vy = r.y + vy;
  const float s = std::sqrt(vx * vx + vy * vy);
  const float m = fmadds(30.0f - s, 0.05f, s) / s;
  vx = vx * m;
  vy = vy * m;
}

void Nucleus::wall(World&, Vec2 n) { reflect(*this, n); }

void Nucleus::on_delete(World& w) {
  for (auto& e : w.entities)
    if (e->type == 0x15) {
      auto& c = static_cast<Cytoplasm&>(*e);
      if (c.nucleus == this) c.nucleus = nullptr;
    }
}

Cytoplasm::Cytoplasm(World& w, float px, float py, Nucleus* n) {
  // Base (sub_82047030): unit "amoebacytoplasm", type code 0x15, flags
  // 0x900.
  x = px;
  y = py;
  unit = w.units.find("amoebacytoplasm");
  type = 0x15;
  flags |= 0x900;
  flags |= 0x14;
  colour[0] = 0.75f;  // dat_82000718
  colour[1] = 0.2f;   // dat_82000D5C
  colour[2] = 0.0f;
  colour[3] = 1.0f;
  nucleus = n;
  age = (float(w.rng.next()) * kTwoPow32) * kTwoPi;
  radius = cell_radius(w, age);
  sx = sy = radius;
  count_enemy(w, *this);
  ++n->cells;
}

void Cytoplasm::update(World& w) {
  age = w.step + age;
  Nucleus* n = nucleus;
  if (!n) {
    // The nucleus is gone: debris and a kill marker, no score, no geoms,
    // no sound.
    Killer k;
    k.x = x;
    k.y = y;
    k.vx = vx;
    k.vy = vy;
    k.kind = 0;
    k.source = this;
    die(w, k, 6);
    return;
  }
  vx = vx + kCytoplasmTendToPosition * (n->x - x);
  vy = (n->y - y) * kCytoplasmTendToPosition + vy;
  vx = vx + (n->vx - vx) * kCytoplasmTendToVelocity;
  vy = vy + (n->vy - vy) * kCytoplasmTendToVelocity;
  vx = vx * kCytoplasmDamp;
  vy = vy * kCytoplasmDamp;
  radius = cell_radius(w, age);
  sx = sy = radius;
  keep_apart(w, radius * 2.0f, 8.0f);  // dat_82000D18
}

void Cytoplasm::keep_apart(World& w, float reach, float strength) {
  Entity* partner = nucleus;
  const uint32_t x0 = uint32_t(int(x - reach) >> 5) & 0x1F, y0 = uint32_t(int(y - reach) >> 5) & 0x1F;
  const uint32_t x1 = uint32_t(int(x + reach) >> 5) & 0x1F, y1 = uint32_t(int(y + reach) >> 5) & 0x1F;
  w.for_each_cell(x0, y0, x1, y1, [&](Entity& o) {
    if ((o.type != type && &o != partner) || &o == this) return;
    const float s = o.radius + radius;
    const float dx = o.x - x, dy = o.y - y;
    const float d2 = dy * dy + dx * dx;
    if (!(d2 < s * s)) return;
    if (d2 < 0.1f) {  // dat_82000714
      const Vec2 r = w.random_vector(1.0f);
      vx = vx + r.x;
      vy = vy + r.y;
      if (&o != partner) {
        const Vec2 q = w.random_vector(1.0f);
        o.vx = o.vx + q.x;
        o.vy = o.vy + q.y;
      }
      return;
    }
    const float d = std::sqrt(dy * dy + dx * dx);
    const float inv = 1.0f / d;
    const float f = (s - d) * strength;
    const float px = (inv * dx) * f, py = (inv * dy) * f;
    vx = vx - px * 0.5f;
    vy = vy - py * 0.5f;
    if (&o == partner) {
      // The nucleus is not moved: the cell takes the whole push.
      vx = vx - px * 0.5f;
      vy = vy - py * 0.5f;
    } else {
      o.vx = o.vx + px * 0.5f;
      o.vy = py * 0.5f + o.vy;
    }
  });
}

void Cytoplasm::wall(World&, Vec2 n) { reflect(*this, n); }

Mat4 Cytoplasm::custom_placement(World& w) {
  Mat4 m = rotation_axis(w.gm, heading, 0, 0, 1);
  for (int c = 0; c < 4; ++c) {
    m.m[c] = m.m[c] * sx;
    m.m[4 + c] = m.m[4 + c] * sy;
    m.m[8 + c] = m.m[8 + c] * 1.0f;
  }
  m.m[12] = m.m[12] + x;
  m.m[13] = m.m[13] + y;
  return m;
}

void Cytoplasm::on_delete(World&) {
  if (nucleus) --nucleus->cells;
}

}  // namespace geow2

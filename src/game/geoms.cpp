#include "game/geoms.h"

#include <cmath>

#include "game/bullets.h"

namespace geow2 {
namespace {

constexpr float kTwoPow32 = 2.32830644e-10f;  // dat_820006E4
constexpr float kLineWidthMote = 1.1f;        // Render.LineWidth.Mote (0x821A1DC8, dat_82000710)
constexpr float kFastSpeed2 = 1024.0f;        // dat_82001C70

// dat_82001D18: the values a geom can take, picked at random (r % 19)
// and walked down to one no larger than what is left to drop.
constexpr int kValues[19] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 5, 5, 10};

// sub_8204DAA0.
int pick_value(World& w, int left) {
  if (left <= 0) return 1;
  for (int i = int(w.rng.next() % 19u); i >= 0; --i)
    if (kValues[i] <= left) return kValues[i];
  return 1;
}

// sub_8204DFF0: one geom of a value up to `left` from the victim; returns
// its value.
int drop_one(World& w, const Entity& victim, int left) {
  const int value = pick_value(w, left);
  auto* g = static_cast<Geom*>(w.add(std::make_unique<Geom>(w, victim, value)));
  // A velocity in the disc of radius 32: random vectors of 32 until one
  // lands inside.
  Vec2 v;
  do {
    v = w.random_vector(32.0f);  // dat_82001140
  } while (!(v.x * v.x + v.y * v.y < kFastSpeed2));
  if (victim.type == 1) {
    g->life = g->life * 1.5f;  // dat_82000C90
    v.x = v.x * 5.0f;          // dat_820006F0
    v.y = v.y * 5.0f;
  }
  const Vec2 off = w.random_vector(6.0f);  // dat_82001414
  g->x = off.x + g->x;
  g->y = off.y + g->y;
  g->vx = v.x;
  g->vy = v.y;
  return value;
}

}  // namespace

GeomShapes GeomShapes::from_image(std::span<const uint8_t> image) {
  GeomShapes s;
  s.small = read_shape(image, 0x82181A58, 8);
  s.large = read_shape(image, 0x82181A98, 24);
  s.other = read_shape(image, 0x82181A24, 6);
  return s;
}

Geom::Geom(World& w, const Entity& victim, int v) {
  // Base sub_82045830 at the victim's position, type code 0x22.
  x = victim.x;
  y = victim.y;
  type = 0x22;
  unit_based = false;
  value = v;
  flags |= 0x1001400;  // glow, drawn by slot 2
  heading = (float(w.rng.next()) * kTwoPow32) * 6.28318548f;  // dat_82181A20
  const Vec2 vel = w.random_vector(16.0f);  // dat_82001104 (replaced by the dropper)
  vx = vel.x;
  vy = vel.y;
  // Life: 10 s in King (mode 5), 4 in Pacifism (mode 4), else 3.25
  // (dat_8200070C, dat_820006F4, dat_82001D7C).
  life = w.mode == 5 ? 10.0f : w.mode == 4 ? 4.0f : 3.25f;
  for (int i = 0; i < 4; ++i) {
    base_colour[i] = w.motes.colour[i];
    colour[i] = base_colour[i];
  }
  sx = std::fmaf(float(w.rng.next()) * kTwoPow32, 0.1f, 0.8f);  // dat_82000714, dat_82000754
  sy = std::fmaf(float(w.rng.next()) * kTwoPow32, 0.1f, 0.8f);
  radius = 3.0f;  // dat_820006F8
  age = (float(w.rng.next()) * kTwoPow32) * 0.5f;  // dat_82000724
  const GeomShapes* s = w.geom_shapes;
  switch (value) {
    case 1:
      shape = s ? &s->small : nullptr;
      sx = sx * 0.6f;  // dat_82001160
      sy = sy * 0.6f;
      break;
    case 2:
      shape = s ? &s->small : nullptr;
      sx = sx * 0.9f;  // dat_82000D70
      sy = sy * 0.9f;
      break;
    case 5:
      shape = s ? &s->large : nullptr;
      break;
    case 10:
      shape = s ? &s->large : nullptr;
      sx = sx * 1.25f;  // dat_82000768
      sy = sy * 1.25f;
      break;
    default:
      shape = s ? &s->other : nullptr;
      sx = sx * 0.8f;
      sy = sy * 0.8f;
      break;
  }
}

void Geom::light(World& w, std::vector<LightVertex>& out) {
  if (hidden) return;
  const float* g = w.motes.glow;
  const float c[3] = {g[0] * g[3], g[1] * g[3], g[2] * g[3]};
  emit_light(out, w.motes.glow_size, x, y, c);
}

void Geom::draw(World& w, LineBatch& out) {
  // sub_82061A30 into the motes' batches (drawn with the units' presets).
  if (!shape) return;
  for (const Segment& s : shape_segments(*shape, x, y, heading, sx, sy, w.gm))
    emit_line(out, kLineWidthMote, 1.0f, s.a, s.b, colour);
}

void Geom::update(World& w) {
  age = w.step + age;
  if (age < life || collector >= 0) {
    // Over the last second, blinking at 15 a second unless being pulled in.
    if (life - 1.0f <= age) {
      if (collector < 0) {
        const float t = float(std::fmod(double(age * 15.0f), 1.0));  // dat_82000D24, sub_8212FC00
        if (!hidden) {
          if (0.5f < t) {
            for (float& c : colour) c = 0.0f;
            hidden = true;
          }
        } else if (t < 0.5f) {
          for (int i = 0; i < 4; ++i) colour[i] = base_colour[i];
          hidden = false;
        }
      } else {
        for (int i = 0; i < 4; ++i) colour[i] = base_colour[i];
        hidden = false;
      }
    }
  } else {
    w.remove(*this);
  }
  // Fast geoms slow down (dat_82001BB8); they spin at 5 radians a second.
  if (kFastSpeed2 < vx * vx + vy * vy) {
    vx = vx * 0.985f;
    vy = vy * 0.985f;
  }
  heading = std::fmaf(w.step, 5.0f, heading);  // dat_820006F0
  // (sub_8204D2E8 then pushes it out of King's zones, list 0x8218143C:
  // with that mode.)
}

void Geom::wall(World& w, Vec2 n) {
  (void)w;
  // Bounce: v -= n * (2 (n . v)) (dat_820006FC).
  const float d = (n.x * vx + n.y * vy) * 2.0f;
  vx = vx - n.x * d;
  vy = vy - n.y * d;
}

void Geom::collect(World& w, size_t p) {
  (void)p;
  // The player's multiplier (dat_8219D0A8) grows by the value, with a
  // pop-up of it ("x%s", 0.75, dat_82001D68), or at each milestone
  // (dat_8219D0AC: +10 below 100, +50 below 1000, else +100) a bigger one
  // 4 below ("%s x%s", ID_X_BONUS_MULTIPLIER, 1.25 s, dat_82001D6C);
  // sounds 0x47 / 0x48 alternate.
  w.multiplier += value;
  w.spawn_geoms += value;  // sub_8203BBB8
  if (w.multiplier < w.multiplier_milestone) {
    w.popups.add_small(x, y, 0.75f, 0, "x" + format_number(w.multiplier));  // dat_82000718
  } else {
    w.popups.add_big(x, y + 4.0f, 1.25f, 0, w.bonus_multiplier_text + " x" + format_number(w.multiplier_milestone));
    const int m = w.multiplier_milestone;
    w.multiplier_milestone = m < 100 ? m + 10 : m < 1000 ? m + 50 : m + 100;
  }
  vx = 0.0f;
  vy = 0.0f;
  w.remove(*this);
}

void drop_geoms(World& w, const Entity& victim, const Killer& k) {
  if (k.kind == 0 || k.kind == 2) return;
  int n = 0;
  switch (victim.type) {
    case 2: case 6: case 0x12: case 0x1F: case 0x21: case 0x24: case 0x25:
      n = 2;
      break;
    case 4: case 10: case 0xC: case 0xD:
      n = 1;
      break;
    case 9:
      n = k.kind != 1 ? 2 : 0;  // spinners shot split instead
      break;
    case 0xB: case 0x14:
      n = 8;
      break;
    case 0x13: case 0x18: case 0x1A: case 0x1B:
      n = 3;
      break;
    case 0x16:
      n = 4;
      break;
    default:
      // Types 3 (5 + 3 x its +0x9C, clamped to 0..12) and 0xE (1 for
      // every fourth segment) count by their own state.
      n = victim.special_geoms();
      break;
  }
  if (w.mode == 5) {
    n = n * 2;
  } else if (w.mode == 3) {
    n = victim.type == 0x16 ? 4 : 1;
  } else if (w.mode == 4) {
    n = victim.type == 0x18 ? 1 : 2;
  }
  while (n > 0) n -= drop_one(w, victim, n);
}

void collect_geoms(World& w) {
  // Up to four players in the game (none in King's zones).
  std::vector<size_t> players;
  for (size_t i = 0; i < w.players.size() && players.size() < 4; ++i)
    if (!w.players[i].removed) players.push_back(i);
  for (size_t gi = 0; gi < w.entities.size(); ++gi) {
    Entity& e = *w.entities[gi];
    if (e.type != 0x22 || (e.flags & kEntityRemoved)) continue;
    Geom& g = static_cast<Geom&>(e);
    int target = g.collector;
    if (target < 0) {
      float best = 0.0f;
      for (size_t i : players) {
        const PlayerTarget& p = w.players[i];
        const float dy = p.y - g.y, dx = p.x - g.x;
        const float d2 = dy * dy + dx * dx;
        const float reach = (p.radius + g.radius) + w.motes.magnet_range;
        // (A player with +0xDC above 0 reaches four times as far.)
        if (d2 < reach * reach && (target < 0 || d2 < best)) {
          best = d2;
          target = int(i);
        }
      }
      if (target < 0) continue;
    }
    g.collector = target;
    const PlayerTarget& p = w.players[size_t(target)];
    const float r = p.radius + g.radius;
    const float dy = p.y - g.y, dx = p.x - g.x;
    const float d2 = dy * dy + dx * dx;
    if (r * r <= d2) {
      // Pulled in: v * 0.825 + direction * 66 (dat_82001D78, dat_82001D74);
      // the direction's length from frsqrte in the original.
      const float inv = 1.0f / std::sqrt(d2);
      g.vx = g.vx * 0.825f + (dx * inv) * 66.0f;
      g.vy = g.vy * 0.825f + (dy * inv) * 66.0f;
    } else {
      g.collect(w, size_t(target));
    }
  }
}

}  // namespace geow2

#include "game/hill.h"

#include <cmath>

namespace geow2 {
namespace {

constexpr int kHillType = 0x1C;

// A channel eased a tenth of the way to its target (dat_82000714).
float ease(float c, float target) { return c + (target - c) * 0.1f; }

}  // namespace

Hill::Hill(World& w, float px, float py) {
  // Base (sub_82047030): unit "hill" (0x82001C14), type code 0x1C, flags
  // 0x900; not counted as an enemy.
  x = px;
  y = py;
  unit = w.units.find("hill");
  type = kHillType;
  flags |= 0x900;
  radius = 1.0f;  // dat_820006C4
  sx = sy = 1.0f;
  flags |= 0x400040;
  // Blue (dat_82000714, dat_820006C4). (The 64-segment ring table it
  // builds is unused: the model draws.) Sound event 0x36.
  colour[0] = 0.1f;
  colour[1] = 0.1f;
  colour[2] = 1.0f;
  colour[3] = 1.0f;
  w.play(audio::Event::kKingCircleSpawn);
}

void Hill::draw(World& w, LineBatch& out) {
  if (!unit || !unit->has_model) return;
  draw_model_part(out, unit->model, 0, placement(w), colour);
}

Mat4 Hill::custom_placement(World&) {
  // scale(r, r, r) (sub_82040F28), then the position added.
  Mat4 m{};
  m.m[0] = radius;
  m.m[5] = radius;
  m.m[10] = radius;
  m.m[15] = 1.0f;
  m.m[12] = 0.0f + x;
  m.m[13] = 0.0f + y;
  return m;
}

void Hill::wall(World&, Vec2 n) {
  const float d = (n.x * vx + n.y * vy) * 2.0f;
  vx = vx - n.x * d;
  vy = vy - n.y * d;
}

void Hill::kill(World& w, const Killer&) { w.remove(*this); }

void Hill::update(World& w) {
  const float dt = w.step;
  // The radius: shrinking by two thirds of R a second in state 3 (to 1),
  // else growing by 2R a second while below R (overshooting it).
  if (state == 3) {
    if (radius <= 1.0f)
      radius = 1.0f;
    else
      radius = std::fmaf(-(dt * 0.6666667f), kHillRadius, radius);  // dat_82000FF4
  } else if (radius < kHillRadius) {
    radius = std::fmaf(kHillRadius * dt, 2.0f, radius);  // dat_820006FC
  }
  sx = sy = radius;
  switch (state) {
    case 0:
      // The first player touching it captures it (sound event 0x4A).
      for (size_t pi = 0; pi < w.players.size(); ++pi) {
        const PlayerTarget& p = w.players[pi];
        if (p.removed) continue;
        const float s = p.radius + radius;
        const float dx = x - p.x, dy = y - p.y;
        if (dy * dy + dx * dx < s * s) {
          state = 1;
          ++w.record_of(pi).zones;
          w.play(audio::Event::kFirstEnterHill);
          break;
        }
      }
      break;
    case 1:
      colour[1] = ease(colour[1], 1.0f);
      colour[0] = ease(colour[0], 0.1f);
      colour[2] = ease(colour[2], 0.1f);
      colour[3] = ease(colour[3], 1.0f);
      timer = timer - dt;
      if (timer <= 1.0f) state = 2;
      break;
    case 2:
      colour[0] = ease(colour[0], 1.0f);
      colour[1] = ease(colour[1], 0.1f);
      colour[2] = ease(colour[2], 0.1f);
      colour[3] = ease(colour[3], 1.0f);
      timer = timer - dt;
      if (timer <= 0.0f) {
        // Its successor (sound event 0x37).
        const Vec2 p = hill_position(w);
        w.add(std::make_unique<Hill>(w, p.x, p.y));
        w.play(audio::Event::kKingCircleFade);
        state = 3;
      }
      break;
    case 3:
      colour[0] = ease(colour[0], 1.0f);
      colour[1] = ease(colour[1], 0.1f);
      colour[2] = ease(colour[2], 0.1f);
      colour[3] = ease(colour[3], 1.0f);
      if (radius <= 1.0f) w.remove(*this);
      break;
    default:
      break;
  }
  if (!kHillSafeZone) return;
  // Units in the cells around it (16 beyond its radius, dat_82001104) that
  // overlap it are moved out to touch its edge: position only.
  const float reach = radius + 16.0f;
  const uint32_t x0 = uint32_t(int(x - reach) >> 5) & 0x1F, y0 = uint32_t(int(y - reach) >> 5) & 0x1F;
  const uint32_t x1 = uint32_t(int(x + reach) >> 5) & 0x1F, y1 = uint32_t(int(y + reach) >> 5) & 0x1F;
  w.for_each_cell(x0, y0, x1, y1, [&](Entity& e) {
    if (e.flags & 3) return;
    const float dx = e.x - x, dy = e.y - y;
    const double d2 = double(dy * dy + dx * dx);
    const float s = e.radius + radius;
    if (!(1.0 <= d2 && d2 <= double(s * s))) return;
    const float k = s / float(std::sqrt(d2));
    e.x = k * dx + x;
    e.y = y + dy * k;
  });
}

Vec2 hill_position(World& w) {
  // Obstacles, at most ten: the players a third of a second ahead
  // (dat_820010F0), then every zone.
  struct Obstacle {
    float x, y, min2;
  };
  Obstacle o[10];
  int n = 0;
  const float near_player = kHillRadius * 1.5f, near_zone = kHillRadius * 2.5f;  // dat_82000C90, dat_82001158
  for (const PlayerTarget& p : w.players) {
    if (p.removed) continue;
    if (n > 9) break;
    o[n++] = {p.x + p.vx * 0.333333343f, p.y + p.vy * 0.333333343f, near_player * near_player};
  }
  for (const auto& e : w.entities) {
    if (e->type != kHillType) continue;
    if (n > 9) break;
    o[n++] = {e->x, e->y, near_zone * near_zone};
  }
  constexpr double kTwoPow31 = 4.656612873077393e-10;  // 2^-31
  Vec2 out{0.0f, 0.0f};
  for (int tries = 0;; ++tries) {
    const double ry = double(w.half_y - kHillRadius), rx = double(w.half_x - kHillRadius);
    const float y = float(double(float(double(w.rng.next()) * kTwoPow31 - 1.0)) * ry);
    const float x = float(double(float(double(w.rng.next()) * kTwoPow31 - 1.0)) * rx);
    out = {x, y};
    bool clear = true;
    for (int i = 0; i < n; ++i) {
      const float dx = x - o[i].x, dy = y - o[i].y;
      if (dx * dx + dy * dy < o[i].min2) {
        clear = false;
        break;
      }
    }
    if (clear || tries + 1 > 0x1F) return out;
  }
}

bool in_hill(const World& w, float radius, float x, float y) {
  for (const auto& e : w.entities) {
    if (e->type != kHillType) continue;
    const float s = e->radius + radius;
    const float dx = x - e->x, dy = y - e->y;
    if (dx * dx + dy * dy < s * s) return true;
  }
  return false;
}

void hill_push_geom(World& w, Entity& g) {
  for (size_t i = 0; i < w.entities.size(); ++i) {
    const Entity& z = *w.entities[i];
    if (z.type != kHillType) continue;
    const float s = z.radius + g.radius;
    const float dx = z.x - g.x, dy = z.y - g.y;
    const double d2 = double(dy * dy + dx * dx);
    if (!(d2 < double(s * s))) continue;
    if (double(0.1f) <= d2) {  // dat_82000714
      const float len = std::sqrt(dy * dy + dx * dx);
      const float inv = float(1.0 / double(len));
      const float k = (s - len) * 1.0f;
      g.vx = g.vx - (inv * dx) * k;
      g.vy = g.vy - (inv * dy) * k;
    } else {
      const Vec2 r = w.random_vector(1.0f);
      g.vx = g.vx + r.x;
      g.vy = r.y + g.vy;
    }
  }
}

void hill_grid_pushes(const World& w, std::vector<GridPush>& out) {
  for (const auto& e : w.entities) {
    if (e->type != kHillType || (e->flags & 0x100011)) continue;
    out.push_back({e->x, e->y, e->radius, 4.0f});
  }
}

}  // namespace geow2

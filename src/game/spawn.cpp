#include "game/spawn.h"

#include <cmath>

#include "game/enemies.h"
#include "game/gate.h"
#include "game/gravity_well.h"
#include "game/snake.h"
#include "game/wanderer.h"

namespace geow2 {

Entity* spawn_enemy(World& w, int type, float angle, float x, float y, bool flag) {
  (void)flag;
  switch (type) {
    case 1:
      return w.add(std::make_unique<Wanderer>(w, x, y));
    case 2:
      return w.add(std::make_unique<Grunt>(w, x, y));
    case 3:
      return w.add(std::make_unique<Weaver>(w, x, y));
    case 4:
      return w.add(std::make_unique<Spinner>(w, x, y));
    case 5:
      // (Unless Game.DontSpawnGravityWells, dat_8219DBB0.)
      return w.add(std::make_unique<GravityWell>(w, x, y));
    case 14: {
      Entity* p = w.add(std::make_unique<Proton>(w, angle, 90.0f, x, y));  // dat_82001100
      p->flags |= kEntityFadingIn;
      return p;
    }
    case 8: {
      auto* head = static_cast<SnakeHead*>(w.add(std::make_unique<SnakeHead>(w, x, y)));
      head->build_segments(w);
      return head;
    }
    case 16:
      return spawn_gate(w, x, y);
    case 12:
    case 13:
      return w.add(std::make_unique<Rocket>(w, angle, x, y, type == 13));
    case 22:
      return w.add(std::make_unique<Duck>(w, x, y));
    default:
      return nullptr;
  }
}

Vec2 spawn_position(World& w, float dist) {
  // Players' positions a third of a second ahead (dat_820010F0), at most
  // four.
  constexpr float kAhead = 0.333333343f;
  float px[4], py[4];
  int n = 0;
  for (const PlayerTarget& p : w.players) {
    if (p.removed || n >= 4) continue;
    px[n] = p.vx * kAhead + p.x;
    py[n] = p.y + p.vy * kAhead;
    ++n;
  }
  const float d2 = dist * dist;
  Vec2 c;
  for (int tries = 0;; ++tries) {
    c = w.random_position();
    bool clear = true;
    for (int i = 0; i < n; ++i) {
      const float dx = c.x - px[i], dy = c.y - py[i];
      if (double(dx * dx + dy * dy) < double(d2)) {
        clear = false;
        break;
      }
    }
    if (clear || tries + 1 > 0x1F) return c;
  }
}

float random_right_angle(World& w) {
  // frac(float(float(k) * pi) * 0.5 / 2pi) * 2pi with k = r & 3, the
  // integer part taken with fctid (round to nearest): 0, pi/2, pi, -pi/2.
  constexpr float kPi = 3.14159274f;                  // dat_820010DC
  constexpr double kInvTwoPi = 0.15915494309137826;    // dat_82000D00 (f64)
  constexpr double kTwoPi = 6.2831853072;              // dat_82000CF8 (f64)
  const float k = float(w.rng.next() & 3);
  const double t = double((k * kPi) * 0.5f) * kInvTwoPi;
  return float((t - std::nearbyint(t)) * kTwoPi);
}

void spawn_at_random(World& w, float dist, int type) {
  const float angle = random_right_angle(w);
  const Vec2 p = spawn_position(w, dist);
  spawn_enemy(w, type, angle, p.x, p.y);
}

Placement placed_position(World& w) {
  // sub_8204F3F8 picks the player spawned earliest; with one player, that
  // player.
  const PlayerTarget* p = nullptr;
  for (const PlayerTarget& t : w.players)
    if (!t.removed) {
      p = &t;
      break;
    }
  if (!p) return {0.0f, 0.0f, random_right_angle(w)};
  // sub_8203D318: candidates on the four walls (x = -292 / 292, y = -192 /
  // 192), the other coordinate clamped (fsel), facing 0, -pi, pi/2, -pi/2.
  auto clamp = [](float v, double lo, double hi) {
    const double a = float(lo - v) < 0.0f ? double(v) : lo;
    return float(float(double(v) - hi) < 0.0f ? a : hi);
  };
  const Placement c[4] = {{-292.0f, clamp(p->y, -192.0, 192.0), 0.0f},
                          {292.0f, clamp(p->y, -192.0, 192.0), -3.14159274f},
                          {clamp(p->x, -292.0, 292.0), -192.0f, 1.57079637f},
                          {clamp(p->x, -292.0, 292.0), 192.0f, -1.57079637f}};
  int best = 0;
  double best_d = 0.0;
  for (int k = 0; k < 4; ++k) {
    const float dx = float(double(p->x) - c[k].x), dy = float(double(p->y) - c[k].y);
    const double d = double(dx * dx + dy * dy);
    if (k == 0 || d < best_d) {
      best = k;
      best_d = d;
    }
  }
  return c[best];
}

}  // namespace geow2

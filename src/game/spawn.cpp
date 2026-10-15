#include "game/spawn.h"

#include <cmath>

#include "game/amoeba.h"
#include "game/enemies.h"
#include "game/gate.h"
#include "game/gravity_well.h"
#include "game/repulsar.h"
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
    case 6:
      return w.add(std::make_unique<Repulsar>(w, x, y));
    case 11: {
      auto* n = static_cast<Nucleus*>(w.add(std::make_unique<Nucleus>(w, x, y)));
      n->start(w);
      return n;
    }
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

const PlayerTarget* first_player(const World& w) {
  for (const PlayerTarget& t : w.players)
    if (!t.removed) return &t;
  return nullptr;
}

Vec2 corner(int k) {
  constexpr float kX = 292.0f, kY = 192.0f;  // dat_82001138 / dat_82001118, dat_82001134 / dat_82001130
  switch (k) {
    case 0: return {-kX, -kY};
    case 1: return {kX, -kY};
    case 2: return {kX, kY};
    case 3: return {-kX, kY};
    default: return {0.0f, 0.0f};
  }
}

Vec2 nearest_corner(World& w) {
  const PlayerTarget* p = first_player(w);
  if (!p) return corner(int(w.rng.next() & 3));
  // Squared distances in float, compared in double; the first minimum.
  Vec2 best;
  double best_d = 0.0;
  for (int k = 0; k < 4; ++k) {
    const Vec2 c = corner(k);
    const float dx = float(double(p->x) - c.x), dy = float(double(p->y) - c.y);
    const double d = double(dx * dx + dy * dy);
    if (k == 0 || d < best_d) {
      best = c;
      best_d = d;
    }
  }
  return best;
}

void spawn_corners(World& w, int type) {
  for (int k = 0; k < 4; ++k) spawn_at_corner(w, type, k);
}

void spawn_at_corner(World& w, int type, int k) {
  const Vec2 c = corner(k);
  spawn_enemy(w, type, 0.0f, c.x, c.y);
}

void spawn_ring(World& w, float radius, int type, int n) {
  const PlayerTarget* p = first_player(w);
  if (!p) return;
  constexpr float kTwoPi = 6.28318548f;  // dat_82181578
  constexpr float kPi = 3.14159274f;     // dat_820010DC
  for (int i = 0; i < n; ++i) {
    const float a = (float(i) / float(n)) * kTwoPi;
    const float x = std::fmaf(float(w.gm.cos(a)), radius, p->x);
    const float y = std::fmaf(float(w.gm.sin(a)), radius, p->y);
    spawn_enemy(w, type, a + kPi, x, y);
  }
}

void spawn_clump(World& w, float spread, int type, Vec2 centre, int n) {
  for (; n > 0; --n) {
    Vec2 q;
    for (int tries = 0; tries < 4; ++tries) {
      const Vec2 off = w.random_vector(spread);
      q = {off.x + centre.x, off.y + centre.y};
      if (-w.half_x <= q.x && q.x <= w.half_x && -w.half_y <= q.y && q.y <= w.half_y) break;
    }
    const float angle = random_right_angle(w);
    spawn_enemy(w, type, angle, q.x, q.y);
  }
}

namespace {

constexpr float kFaceDown = 1.57079637f;    // dat_8200071C: pi/2
constexpr float kFaceUp = -1.57079637f;     // dat_820010F4
constexpr float kFaceLeft = -3.14159274f;   // dat_820010F8

// One line of the walls: from `start` while below `end` in steps of 20
// (dat_82000CE8, each sum rounded to float), along x at y = `fixed` or
// along y at x = `fixed`.
void line(World& w, int type, bool along_x, float fixed, float start, float end, float angle, bool flag) {
  for (float v = start; v < end; v = v + 20.0f) {
    if (along_x)
      spawn_enemy(w, type, angle, v, fixed, flag);
    else
      spawn_enemy(w, type, angle, fixed, v, flag);
  }
}

}  // namespace

void spawn_wall(World& w, int type, int side, bool flag) {
  const float hx = w.half_x, hy = w.half_y;
  // Starting 10 (dat_8200070C) in from the corner.
  switch (side) {
    case 0: line(w, type, true, -hy, 10.0f - hx, hx, kFaceDown, flag); break;
    case 1: line(w, type, true, hy, 10.0f - hx, hx, kFaceUp, flag); break;
    case 2: line(w, type, false, -hx, 10.0f - hy, hy, 0.0f, flag); break;
    case 3: line(w, type, false, hx, 10.0f - hy, hy, kFaceLeft, flag); break;
    default: break;
  }
}

void spawn_half_wall(World& w, int type, int side, bool other) {
  const float hx = w.half_x, hy = w.half_y;
  if (!other) {
    switch (side) {
      case 0: line(w, type, true, -hy, 10.0f - hx, 0.0f, kFaceDown, false); break;
      case 1: line(w, type, true, hy, 10.0f, hx, kFaceUp, false); break;
      case 2: line(w, type, false, -hx, 10.0f - hy, 0.0f, 0.0f, false); break;
      case 3: line(w, type, false, hx, 10.0f, hy, kFaceLeft, false); break;
      default: break;
    }
  } else {
    switch (side) {
      case 0: line(w, type, true, hy, 10.0f - hx, 0.0f, kFaceUp, false); break;
      case 1: line(w, type, true, -hy, 10.0f, hx, kFaceDown, false); break;
      case 2: line(w, type, false, hx, 10.0f - hy, 0.0f, kFaceLeft, false); break;
      case 3: line(w, type, false, -hx, 10.0f, hy, 0.0f, false); break;
      default: break;
    }
  }
}

}  // namespace geow2

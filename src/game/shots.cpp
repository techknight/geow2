#include "game/shots.h"

#include <cmath>

#include "game/gate.h"

namespace geow2 {
namespace {

void sweep(World& w, Bullet& b) {
  // Segment from the start of the step to now, its middle and length.
  const double dx = double(b.x - b.prev_x), dy = double(b.y - b.prev_y);
  const float mx = float(dx * 0.5) + b.prev_x, my = float(dy * 0.5) + b.prev_y;  // dat_82000724
  const double len = std::sqrt(double(float(dy * dy) + float(dx * dx)));
  const float reach = float(len * 0.5 + double(b.radius)) + 8.0f;  // dat_82000D18
  const float ux = float(double(float(1.0 / len)) * dx), uy = float(double(float(1.0 / len)) * dy);
  bool done = false;
  w.for_each_near(mx, my, reach, [&](Entity& o) {
    if (done) return;
    // Player shots test entities with flag 4 (touchable enemies).
    if ((o.flags & 0x20011) || !(o.flags & kEntityTouches)) return;
    const float r = float(double(o.radius) + double(b.radius));
    const double proj = double((o.y - b.prev_y) * uy + (o.x - b.prev_x) * ux);
    // Clamped to the segment (two fsel).
    const double lo = -proj < 0.0 ? proj : 0.0;
    const double t = float(proj - len) < 0.0f ? lo : len;
    const float px = ux * float(t) + b.prev_x, py = uy * float(t) + b.prev_y;
    const float ex = px - o.x, ey = py - o.y;
    if (!(ey * ey + ex * ex < r * r)) return;
    b.x = px;
    b.y = py;
    // dat_8219D248 == 0: the entity's slot 7 with (shot, owner, kind 1).
    Killer k;
    k.x = b.x;
    k.y = b.y;
    k.vx = b.vx;
    k.vy = b.vy;
    k.kind = 1;
    k.object_type = 5;
    k.shot_bounces = b.bounces;
    k.player = b.owner;
    o.hit_by_shot(w, k);
    // The shot's slot 6 (sub_82051E58): gone, unless what it hit bounces
    // it (type codes 0x11 bacteria and 0x1F pin, which no mode spawns).
    b.removed = true;
    done = true;
  });
}

}  // namespace

void shot_collisions(World& w, std::vector<Bullet>& shots) {
  for (Bullet& b : shots) {
    if (b.removed) continue;
    bounce_off_beams(w, b);  // sub_82051B18
    sweep(w, b);
  }
}

}  // namespace geow2

#include "game/shockwave.h"

#include <cmath>

namespace geow2 {
namespace {

// Unit.SmartBomb.PlayerColour (0x8219FFB0) and .EnemyColour (0x8219FFF4).
constexpr float kPlayerColour[4] = {0.05f, 0.5f, 1.5f, 1.0f};
constexpr float kEnemyColour[4] = {3.0f, 0.5f, 0.1f, 1.0f};

}  // namespace

Shockwave::Shockwave(World& w, float max_r, float s, float px, float py, int k) {
  x = px;
  y = py;
  type = 0xF;
  unit_based = false;
  flags |= kEntityHasModel;  // 0x400: drawn by its own slot 2
  max_radius = max_r;
  speed = s;
  kind = k;
  // The first radius: one step's growth, or the maximum if smaller.
  const float first = w.step * speed;
  r = max_radius - first < 0.0f ? max_radius : first;
  blast(w);
}

void Shockwave::update(World& w) {
  if (!(r < max_radius)) {
    w.remove(*this);
    return;
  }
  const float grown = std::fmaf(speed, w.step, r);
  r = max_radius - grown >= 0.0f ? grown : max_radius;
  blast(w);
}

void Shockwave::blast(World& w) {
  // Kind 0 (the bomb): sub_82055DA0, kill tag 4 (no score); everything
  // within the radius not removed, a player or fading in, and not of type
  // code 0x1C. Kind 1 (a gate's, sub_820564C0) and kind 2 (a well's,
  // sub_82056120): tag 3, reaching what touches the ring (its radius
  // added); kind 1 spares gates, kind 2 hits wells (slot 7) instead.
  Killer k;
  k.x = x;
  k.y = y;
  k.kind = kind == 0 ? 4 : 3;
  k.object_type = 0xF;
  k.blast_kind = kind;
  k.source = this;
  std::vector<Entity*> hit;
  w.for_each_near(x, y, r, [&](Entity& e) {
    if ((e.flags & 0x13) || e.type == 0x1C || &e == this) return;
    if (kind == 1 && e.type == 0x18) return;
    const float dx = e.x - x, dy = e.y - y;
    const float reach = kind == 0 ? r : e.radius + r;
    if (dy * dy + dx * dx <= reach * reach) hit.push_back(&e);
  });
  // The original calls the slots inside the walk: what an earlier kill
  // removed is skipped.
  for (Entity* e : hit) {
    if (e->flags & 0x13) continue;
    if (kind == 2 && e->type == 3) {
      e->hit_by_shot(w, k);
      continue;
    }
    e->kill(w, k);
    ++w.bomb_kills;
  }
  // (A second walk over the hash at dat_8219F9AC removes what has flag
  // 0x200000: comes with its users.)
}

void Shockwave::draw(World& w, LineBatch& out) {
  // 128 segments, four rings at radius * (1 + j * 0.05) (at most the
  // maximum), cap size 0.5.
  const float* ring = kind == 0 ? kPlayerColour : kEnemyColour;
  const float step = 6.28318548f * 0.0078125f;  // dat_82181FF0, dat_8200231C
  for (int i = 0; i < 128; ++i) {
    const float a0 = float(i) * step, a1 = float(i + 1) * step;
    const float c0 = float(w.gm.cos(a0)), s0 = float(w.gm.sin(a0));
    const float c1 = float(w.gm.cos(a1)), s1 = float(w.gm.sin(a1));
    for (int j = 0; j < 4; ++j) {
      float rr = std::fmaf(float(j), 0.05f, 1.0f) * r;  // dat_82000D60
      rr = max_radius - rr >= 0.0f ? rr : max_radius;
      const Vec4 p0{std::fmaf(c0, rr, x), std::fmaf(s0, rr, y), 0, 0};
      const Vec4 p1{std::fmaf(c1, rr, x), std::fmaf(s1, rr, y), 0, 0};
      emit_line(out, kLineWidthUnit, 0.5f, p0, p1, ring);
    }
  }
}

}  // namespace geow2

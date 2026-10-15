#include "game/world.h"

#include <algorithm>
#include <cmath>

#include "game/geoms.h"
#include "game/rotation.h"

namespace geow2 {
namespace {

constexpr float kTwoPow31 = 4.65661287e-10f;  // dat_820006C8
constexpr float kTwoPow32 = 2.32830644e-10f;  // dat_820006E4
constexpr bool kUseModels = true;             // Render.UseModels (0x8219E82C, default 1)

// sub_8212FC00(x, 1.0) as the fade effects call it: the fractional part.
float frac_of(float v) { return float(std::fmod(double(v), 1.0)); }

}  // namespace

void draw_model_part(LineBatch& out, const ModelGeometry& g, size_t part, const Mat4& m, const float colour[4]) {
  if (part >= g.parts.size()) return;
  for (const ModelLine& l : g.parts[part]) {
    const float c[4] = {colour[0] * l.r, l.g * colour[1], l.b * colour[2], l.a * colour[3]};
    emit_line(out, kLineWidthUnit, g.cap_size, mul(l.p0, m), mul(l.p1, m), c);
  }
}

void Entity::light(World& w, std::vector<LightVertex>& out) {
  (void)w;
  if (!unit) return;
  const float* g = unit->glow;
  float c[3];
  if (!(flags & kEntityFadingIn)) {
    const float a = g[3];
    c[0] = a * g[0];
    c[1] = g[1] * a;
    c[2] = g[2] * a;
  } else {
    // Pulsing while fading in: times fmod(fade * 3, 1) * 0.5 + 1.
    const float k = std::fmaf(frac_of(fade * 3.0f), 0.5f, 1.0f);
    const float a = g[3];
    c[0] = (a * g[0]) * k;
    c[1] = (g[1] * a) * k;
    c[2] = (g[2] * a) * k;
  }
  emit_light(out, unit->glow_size, x, y, c);
}

void Entity::draw(World& w, LineBatch& out) {
  const float white[4] = {1, 1, 1, 1};
  draw_unit(w, out, white);
}

void Entity::draw_unit(World& w, LineBatch& out, const float tint[4]) {
  if (!unit || !unit->has_model) return;
  const Mat4 m = placement(w);
  if (!(flags & kEntityFadingIn)) {
    draw_model_part(out, unit->model, 0, m, tint);
    return;
  }
  // Fading in: two ghosts from g = fmod(fade * 3, 1) + i: alpha g folded
  // back above 1 (2 - g), size g * 1.4 + 0.1 unfolded (sub_82046D88).
  for (int i = 0; i < 2; ++i) {
    const float g = frac_of(fade * 3.0f) + float(i);
    float f = g;
    if (f >= 1.0f) f = float(2.0 - double(f));
    if (f < 0.025f) continue;  // dat_82000D64
    const float s = std::fmaf(g, 1.4f, 0.1f);  // dat_82000FF8, dat_82000714
    Mat4 scaled = m;
    for (int r = 0; r < 3; ++r)
      for (int c = 0; c < 4; ++c) scaled.m[r * 4 + c] = m.m[r * 4 + c] * s;  // sub_82040F28 then A * B
    const float ghost[4] = {tint[0], tint[1], tint[2], tint[3] * f};
    draw_model_part(out, unit->model, 0, scaled, ghost);
  }
}

void Entity::post_move(World& w) {
  // Move to the hash cell of the current position (tail of the cell's list).
  const int c = World::cell_of(x, y);
  if (c != cell) {
    if (cell >= 0) {
      auto& v = w.cells[size_t(cell)];
      v.erase(std::find(v.begin(), v.end(), this));
    }
    w.cells[size_t(c)].push_back(this);
    cell = c;
  }
  if (unit) {
    if (unit->glow_size > 0.0f) flags |= kEntityGlow;
    if (unit->has_model && kUseModels)
      flags |= kEntityHasModel;
    else
      flags &= ~kEntityHasModel;
  }
}

Mat4 Entity::custom_placement(World& w) { return placement_style_none(w.gm, x, y, heading); }

void Entity::die(World& w, const Killer& k, uint32_t f) {
  if (flags & kEntityRemoved) return;
  if (!(f & 1)) {
    if (unit) {
      w.effects.trigger(unit->killed, x, y);
      const float white[4] = {1, 1, 1, 1};  // dat_820006C4
      w.shatter(unit, placement(w), x, y, radius, k, (f & 0x10) ? colour : white);
    }
    w.death_markers.push_back({x, y});
  }
  if (!(f & 2)) {
    w.award(*this, k);
    if (w.geom_shapes) drop_geoms(w, *this, k);
  }
  if (!(f & 4)) w.play(audio::Event::kEnemyExplode);
  if (!(f & 8)) w.remove(*this);
}

Mat4 Entity::placement(World& w) {
  const UnitStyle style = unit ? unit->style : UnitStyle::kNone;
  switch (style) {
    case UnitStyle::kTiltVelocity: {
      const Mat4 basis = look_at_basis({x, y, 0, 1}, {vx, vy, 100.0f, 0}, unit->up);  // dat_82001020
      return mul(rotation_axis(w.gm, heading, 0, 0, 1), basis);
    }
    case UnitStyle::kCustom:
      return custom_placement(w);
    default:
      return placement_style_none(w.gm, x, y, heading);
  }
}

Entity* World::add(std::unique_ptr<Entity> e) {
  e->order = next_order++;
  entities.push_back(std::move(e));
  Entity* p = entities.back().get();
  // Unit-based entities join the spatial hash (sub_82047030 places them
  // in their cell); the others stay out of it.
  if (p->unit_based) p->post_move(*this);
  return p;
}

void World::remove(Entity& e) { e.flags |= kEntityRemoved; }

void World::new_game() {
  entities.clear();
  for (auto& c : cells) c.clear();
  enemy_count = 0;
  frozen = false;
  bomb_kills = 0;
  clock = 0.0f;
  spawn_kills = spawn_geoms = 0;
  scoring_open = true;
  gate_chain = 0;
  gate_chain_time = 0.0f;
  death_markers.clear();
  score = 0;
  multiplier = 1;
  multiplier_milestone = 10;
  kills = 0;
  zones_captured = 0;
  bounce_kills = 0;
  bounce_hint = 0.0f;
  spawned_type.fill(false);
}

void World::shatter(const UnitInfo* u, const Mat4& m, float px, float py, float radius, const Killer& k,
                    const float colour[4]) {
  if (!u || !u->has_model || u->model.parts.empty()) return;
  float ix = k.x, iy = k.y;
  const float dx = ix - px, dy = iy - py;
  const float d = std::sqrt(dy * dy + dx * dx);
  const float inv = 1.0f / d;
  if (0.1f < d) {  // dat_82000714
    ix = px + ((inv * dx) * radius) * 0.9f;  // dat_82000D70
    iy = py + ((inv * dy) * radius) * 0.9f;
  }
  debris.smash(effects, rng, 1.0f, u->model.parts[0], m, ix, iy, k.vx, k.vy, colour);
}

void World::purge() {
  for (auto it = entities.begin(); it != entities.end();) {
    Entity& e = **it;
    if (!(e.flags & kEntityRemoved)) {
      ++it;
      continue;
    }
    if (e.cell >= 0) {
      auto& v = cells[size_t(e.cell)];
      v.erase(std::find(v.begin(), v.end(), &e));
    }
    // The unit destructor (sub_82046B08) takes it off the enemy count.
    if (e.flags & kEntityCounted) --enemy_count;
    e.on_delete(*this);
    it = entities.erase(it);
  }
}

void World::award(const Entity& victim, const Killer& k) {
  // sub_820408D0. Points by the victim's type code (+0x44).
  if (k.kind == 0 || k.kind == 2 || k.kind == 4) return;
  if (!scoring_open) return;
  ++kills;
  int base = 0;
  switch (victim.type) {
    case 2: case 4: case 10: case 0x1F: case 0x25: base = 50; break;
    case 6: case 9: case 0x13: base = 100; break;
    case 0xB: base = 350; break;
    case 0xC: case 0x11: base = 10; break;
    case 0xD: base = 150; break;
    case 0x12: case 0x21: base = 25; break;
    case 0x14: base = 500; break;
    case 0x1B: base = 75; break;
    default: base = victim.special_points(*this); break;  // types 3, 0x16, 0x18
  }
  // A gate end killed with the chain past 4: Slalom (bit 1).
  if (victim.type == 0x18 && gate_chain > 4 && achieve) achieve(0x001, 0);
  // sub_8203BAC8: the spawners' kill weight.
  switch (victim.type) {
    case 2: case 0x16: spawn_kills += 2; break;
    case 6: case 9: spawn_kills += 4; break;
    case 0xD: spawn_kills += 8; break;
    case 0x12: case 0x21: spawn_kills += 1; break;
    default: break;
  }
  // multiplier * points * 0.25 (dat_82000720), doubled in King, rounded
  // down to a multiple of 5.
  int64_t fives = int64_t(float(multiplier) * float(base) * 0.25f);
  if (king_mode) fives <<= 1;
  fives /= 5;
  int64_t points = fives * 5;
  if (points < 1) return;
  // Shots that bounced off a gate (kind 5's +0xA0 > 0) score five times.
  // Killed by a well or a gate, by their blasts (kinds 1 and 2), or by a
  // shot that bounced off a gate's beam: five times the points.
  if ((k.object_type == 3 || k.object_type == 0x18) || (k.object_type == 0xF && (k.blast_kind == 1 || k.blast_kind == 2)) ||
      (k.object_type == 5 && k.shot_bounces > 0))
    points = fives * 25;
  if (k.object_type == 5 && k.shot_bounces > 0 && mode == 0) {
    // Deadline counts these for Rebound, the hint showing while below 76.
    ++bounce_kills;
    if (bounce_kills < 0x4C) bounce_hint = 4.0f;  // dat_820006F4
  }
  score += points;
  if (scored) scored();
  // The points linger where the victim was (size 1, the player's colour:
  // index 0 in single player; "%s", dat_82000CD4).
  popups.add_small(victim.x, victim.y, 1.0f, 0, format_number(points));
}

int World::cell_of(float x, float y) { return (int(y) & 0x3E0) | (int(x) >> 5 & 0x1F); }

void World::update_entities() {
  // The object list in creation order: the entities and the player shots
  // interleaved (a shot is an object like any other, sub_82052DB8);
  // entities created during the walk are visited too, after everything
  // else. A shot that hit a wall last step goes now (sub_82052418).
  size_t j = 0;
  for (size_t i = 0;;) {
    if (shots && j < shots->size() && (i >= entities.size() || (*shots)[j].order < entities[i]->order)) {
      Bullet& b = (*shots)[j++];
      const bool was_hit = b.hit;
      if (!b.removed && !bullet_step(b, step, half_x, half_y, effects)) b.removed = true;
      if (!was_hit && b.hit) play(audio::Event::kBulletHitWall);  // sub_82051AF0's first contact
      continue;
    }
    if (i >= entities.size()) break;
    Entity& e = *entities[i++];
    if (e.flags & 3) continue;
    // sub_820453E0.
    const uint32_t entry = e.flags;
    if (entry & kEntityFadingIn) {
      e.fade = std::fmaf(step, 1.5f, e.fade);  // dat_82000C90
      if (e.fade >= 1.0f) {
        e.fade = 1.0f;
        e.flags = entry & ~kEntityFadingIn;
      }
    }
    if (!(e.flags & kEntityFadingIn)) {
      e.update(*this);
      if (!frozen || (e.flags & 0xA)) {
        e.x = e.x + e.vx * step;
        e.y = e.vy * step + e.y;
      }
      if (e.flags & kEntityPostMove) e.post_move(*this);
    }
    // Arena edge (sub_8205C5D8 for non-players; skipped with 0x800000
    // unless 0x8 is set).
    Vec2 n;
    const bool skip_edge = !(e.flags & kEntityPlayer) && !(e.flags & 0x8) && (e.flags & 0x800000);
    if (!skip_edge && arena_contact(e.radius, half_x, half_y, e.x, e.y, n)) e.wall(*this, n);
    // Touching a player (player vtable slot 12).
    if (!(entry & kEntityFadingIn) && (e.flags & 0x144) == 0x104) {
      for (size_t k = 0; k < players.size(); ++k) {
        const PlayerTarget& p = players[k];
        if (p.removed) continue;
        const float r = p.radius + e.radius;
        const float dx = p.x - e.x, dy = p.y - e.y;
        if (dy * dy + dx * dx <= r * r && touch) touch({k, &e});
      }
    }
  }
}

void World::light_entities(std::vector<LightVertex>& lights) {
  for (auto& p : entities)
    if (!(p->flags & kEntityRemoved) && (p->flags & kEntityGlow)) p->light(*this, lights);
}

void World::draw_entities(LineBatch& out) {
  // Slot 2 for models; the others draw their shape table (sub_82061908),
  // which only the beam ghost uses so far.
  for (auto& p : entities)
    if (!(p->flags & kEntityRemoved)) p->draw(*this, out);
}

const PlayerTarget* World::nearest_player(const Entity& e) const {
  const PlayerTarget* best = nullptr;
  float best_d = 0.0f;
  for (const PlayerTarget& p : players) {
    if (p.removed || p.hidden > 0.0f) continue;
    const float dx = e.x - p.x, dy = e.y - p.y;
    const float d = dx * dx + dy * dy;
    if (!best || d < best_d) {
      best_d = d;
      best = &p;
    }
  }
  return best;
}

const PlayerTarget* World::target(const Entity& e) const {
  if (!e.owner) return nearest_player(e);
  for (const PlayerTarget& p : players)
    if (!p.removed) return &p;
  return nullptr;
}

Vec2 World::random_vector(float scale) {
  const float y = float(double(std::fmaf(float(rng.next()), kTwoPow31, -1.0f)) * scale);
  const float x = float(double(std::fmaf(float(rng.next()), kTwoPow31, -1.0f)) * scale);
  return {x, y};
}

float World::random_float(float scale) { return (float(rng.next()) * kTwoPow32) * scale; }

Vec2 World::random_position() {
  const float y = std::fmaf(float(rng.next()), kTwoPow31, -1.0f) * half_y;
  const float x = std::fmaf(float(rng.next()), kTwoPow31, -1.0f) * half_x;
  return {x, y};
}

void World::separate(Entity& e, float radius, float strength) {
  for_each_near(e.x, e.y, radius, [&](Entity& o) {
    if (o.type != e.type || &o == &e) return;
    const double rsum = double(o.radius + e.radius);
    const float dx = o.x - e.x, dy = o.y - e.y;
    const double d2 = double(dy * dy + dx * dx);
    if (!(d2 < double(float(rsum * rsum)))) return;
    if (0.1 <= d2) {  // dat_82000714
      const double d = std::sqrt(d2);
      const float inv = float(1.0 / d);
      const float f = float(double(float(rsum - d)) * strength);
      const double px = double((inv * dx) * f), py = double((inv * dy) * f);
      e.vx = e.vx - float(px * 0.5);
      e.vy = e.vy - float(py * 0.5);
      o.vx = o.vx + float(px * 0.5);
      o.vy = o.vy + float(py * 0.5);
    } else {
      const Vec2 a = random_vector(1.0f);
      e.vx = e.vx + a.x;
      e.vy = a.y + e.vy;
      const Vec2 b = random_vector(1.0f);
      o.vx = b.x + o.vx;
      o.vy = b.y + o.vy;
    }
  });
}

}  // namespace geow2

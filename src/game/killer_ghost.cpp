#include "game/killer_ghost.h"

#include <algorithm>

#include "game/bullets.h"
#include "game/gate.h"
#include "game/snake.h"

namespace geow2 {

KillerGhost::KillerGhost(World& w, Entity& killer, const float tint[4]) {
  // Base sub_82045830 at the killer's position, type 7; heading, scale and
  // colour copied; radius 0; flags 0xC0 (never touches).
  x = killer.x;
  y = killer.y;
  type = 7;
  unit_based = false;
  heading = killer.heading;
  sx = killer.sx;
  sy = killer.sy;
  radius = 0.0f;
  flags |= 0xC0;
  for (int i = 0; i < 4; ++i) {
    colour[i] = killer.colour[i];
    base_colour[i] = killer.colour[i];
  }
  killer_type = killer.type;
  for (int i = 0; i < 4; ++i) draw_colour[i] = tint[i];
  if (killer.shape_points > 0) killer_points = killer.shape_points;
  // A touchable unit with a model leaves its model and placement.
  if ((killer.flags & kEntityTouchable) && killer.unit && killer.unit->has_model) {
    model = &killer.unit->model;
    world = killer.placement(w);
    if (!model->parts.empty()) flags |= kEntityHasModel;
  }
}

KillerGhost::KillerGhost(World& w, float ax, float ay, float bx, float by, const float c[4]) {
  (void)w;
  x = (ax + bx) * 0.5f;
  y = (ay + by) * 0.5f;
  type = 7;
  unit_based = false;
  flags |= 0xC0;
  for (int i = 0; i < 4; ++i) {
    colour[i] = c[i];
    base_colour[i] = c[i];
  }
  killer_type = 0x18;
  segment = {{ax - x, ay - y}, {bx - x, by - y}};
  killer_points = 2;
}

void KillerGhost::update(World& w) {
  time = time - w.step;
  if (time <= 0.0f) w.remove(*this);
  // The colour fades over the last quarter second (min(max(t * 4, 0), 1)).
  const float k = std::min(std::max(time * 4.0f, 0.0f), 1.0f);  // dat_820006F4
  colour[3] = base_colour[3];
  colour[0] = k * base_colour[0];
  colour[1] = base_colour[1] * k;
  colour[2] = base_colour[2] * k;
  // Every fifth step the shape table is swapped in or out.
  if (++blink > 4) {
    blink = 0;
    shown = !shown && killer_points > 0;
  }
}

void KillerGhost::draw(World& w, LineBatch& out) {
  if (shown && !segment.empty()) {
    // The generic shape draw (sub_82061908) in its fading colour.
    for (const Segment& s : shape_segments(segment, x, y, heading, sx, sy, w.gm))
      emit_line(out, kLineWidthUnit, 1.0f, s.a, s.b, colour);
    return;
  }
  if (!shown || !model) return;
  draw_model_part(out, *model, 0, world, draw_colour);
  if (killer_type == 0xB) draw_model_part(out, *model, 1, world, draw_colour);
  // Then a light sprite of radius * 4 in the colour times 8 (sub_8205FA98):
  // the ghost's radius is 0, so it covers nothing.
}

void replace_killer(World& w, Entity& killer) {
  const float white[4] = {1, 1, 1, 1};  // dat_820006C4
  if (killer.type == 0xD || killer.type == 0xE) {
    Entity* head = killer.type == 0xD ? &killer : snake_head_of(killer);
    if (!head) return;
    w.add(std::make_unique<KillerGhost>(w, *head, white));
    w.remove(*head);
    // Every segment of that head (list 0x82181414, construction order),
    // tinted with its own colour.
    for (size_t i = 0; i < w.entities.size(); ++i) {
      Entity& e = *w.entities[i];
      if (e.type != 0xE || snake_head_of(e) != head) continue;
      const float tint[4] = {e.colour[0], e.colour[1], e.colour[2], e.colour[3]};
      w.add(std::make_unique<KillerGhost>(w, e, tint));
      w.remove(e);
    }
    return;
  }
  if (killer.type == 3) {
    // A gravity well: in its colour times 0.35 (dat_82000704).
    const float tint[4] = {killer.colour[0] * 0.35f, killer.colour[1] * 0.35f, killer.colour[2] * 0.35f,
                           killer.colour[3] * 0.35f};
    w.add(std::make_unique<KillerGhost>(w, killer, tint));
    w.remove(killer);
    return;
  }
  if (killer.type == 0x18) {
    // Both ends, and the beam between them ((0.55875, 0.97, 2.85, 1),
    // dat_82001844 / 82001840 / 8200183C).
    auto& end = static_cast<GateEnd&>(killer);
    w.add(std::make_unique<KillerGhost>(w, killer, white));
    w.remove(killer);
    if (GateEnd* p = end.partner) {
      w.add(std::make_unique<KillerGhost>(w, *p, white));
      w.remove(*p);
      const float beam[4] = {0.558749974f, 0.970000029f, 2.8499999f, 1.0f};
      w.add(std::make_unique<KillerGhost>(w, end.x, end.y, p->x, p->y, beam));
    }
    return;
  }
  w.add(std::make_unique<KillerGhost>(w, killer, white));
  w.remove(killer);
}

}  // namespace geow2

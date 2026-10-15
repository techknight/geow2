#include "game/killer_ghost.h"

#include <algorithm>

namespace geow2 {

KillerGhost::KillerGhost(World& w, Entity& killer) {
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
  if (killer.shape_points > 0) killer_points = killer.shape_points;
  // A touchable unit with a model leaves its model and placement.
  if ((killer.flags & kEntityTouchable) && killer.unit && killer.unit->has_model) {
    model = &killer.unit->model;
    world = killer.placement(w);
    if (!model->parts.empty()) flags |= kEntityHasModel;
  }
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
  (void)w;
  if (!shown || !model) return;
  draw_model_part(out, *model, 0, world, draw_colour);
  if (killer_type == 0xB) draw_model_part(out, *model, 1, world, draw_colour);
  // Then a light sprite of radius * 4 in the colour times 8 (sub_8205FA98):
  // the ghost's radius is 0, so it covers nothing.
}

void replace_killer(World& w, Entity& killer) {
  w.add(std::make_unique<KillerGhost>(w, killer));
  w.remove(killer);
}

}  // namespace geow2

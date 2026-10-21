#include "app/smooth.h"

#include "app/profile.h"

namespace geow2 {

void SmoothFrames::after_step(const Game& g) {
  cur_ ^= 1;
  Shot& c = shots_[cur_];
  c.scene = g.scene;
  c.moving = !frozen(g);
  c.camera = g.camera;
  foreground(g, c.front, false);
  c.grid = g.grid_lines;
  if (kept_ < 2) ++kept_;
}

bool SmoothFrames::blendable(const Game& g) const {
  const Shot& p = shots_[cur_ ^ 1];
  const Shot& c = shots_[cur_];
  // While frozen nothing moves (and the debris and particles still hold the
  // last move); the debug camera's own view is drawn as it is.
  return kept_ == 2 && c.moving && p.scene == c.scene && c.scene == g.scene && !g.debug_camera;
}

void SmoothFrames::render(Renderer& r, SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* target, SDL_GPUTextureFormat format,
                          uint32_t w, uint32_t h, const Game& g, const UiCanvas& ui, float s) {
  if (!(s < 1.0f) || !blendable(g)) {
    geow2::render(r, cmd, target, format, w, h, g, ui);
    return;
  }
  const Shot& p = shots_[cur_ ^ 1];
  const Shot& c = shots_[cur_];
  {
    PROFILE("smooth blend");
    lines_.blend(p.front, c.front, s, front_);
    // The debris after the objects, as foreground() adds it.
    g.world->debris.draw_between(front_, s);
    if (g.grid_visible) blend_grid(p.grid, c.grid, s, grid_);
    shift_streaks(g.particle_quads, g.world->particle_tuning.line_height, s, particles_);
  }
  PROFILE("render build+record");
  const Mat4 view = camera_view_projection(blend_camera(p.camera, c.camera, s), *g.gm);
  r.draw(cmd, target, format, w, h, view, g.star_vertices, g.grid_visible ? &grid_ : nullptr, g.walls, particles_,
         g.linger_lines, front_, ui, g.ui, camera_view_projection(CameraState{}, *g.gm));
}

}  // namespace geow2

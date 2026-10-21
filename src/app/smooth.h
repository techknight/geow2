#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <vector>

#include "app/game.h"
#include "game/blend.h"

namespace geow2 {

// The port's smooth motion in the window (DIFFERENCES.md, Frame timing):
// after each step a copy of what that step would draw is kept, and a frame
// between steps is drawn a fraction `s` of the way from the last step's
// copy to this one's (game/blend.h).
class SmoothFrames {
 public:
  // After each step (its light map update included).
  void after_step(const Game& g);
  // Forgets the steps kept (smooth motion turned off or on).
  void reset() { kept_ = 0; }
  // The frame at `s` (0 < s <= 1) into `target`; at s = 1, or when there is
  // nothing to blend (one step kept, a new scene, the game frozen), exactly
  // render()'s picture.
  void render(Renderer& r, SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* target, SDL_GPUTextureFormat format,
              uint32_t w, uint32_t h, const Game& g, const UiCanvas& ui, float s);

 private:
  struct Shot {
    uint32_t scene = 0;
    bool moving = false;  // the step moved the world (not frozen)
    CameraState camera;
    LineBatch front;  // foreground() without the debris
    std::vector<GridVertex> grid;
  };
  bool blendable(const Game& g) const;

  Shot shots_[2];
  int cur_ = 0;
  int kept_ = 0;
  LineBlend lines_;
  LineBatch front_;
  std::vector<GridVertex> grid_;
  std::vector<LineVertex> particles_;
};

}  // namespace geow2

#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "game/camera.h"
#include "game/light_map.h"
#include "game/line_quads.h"

namespace geow2 {

// The port's smooth motion (DIFFERENCES.md, Frame timing): on a display
// faster than 60 Hz the frames between two steps are drawn a fraction `s`
// (0 < s < 1) of the way from the last step's picture to this step's, so
// that a frame shows step N-1+s. Only what is drawn is blended; the
// simulation never sees it.

// The foreground lines: each object's range (LineBatch::mark) whose key the
// last step's batch has with as many lines is blended vertex by vertex
// (position only; colours as now); anything else is drawn as now.
class LineBlend {
 public:
  void blend(const LineBatch& prev, const LineBatch& cur, float s, LineBatch& out);

 private:
  std::unordered_map<uint64_t, size_t> prev_ranges_;
};

// The grid's line quads: the same mesh every step, so blended vertex by
// vertex when the counts agree (else drawn as now).
void blend_grid(const std::vector<GridVertex>& prev, const std::vector<GridVertex>& cur, float s,
                std::vector<GridVertex>& out);

// The particle streaks (Particles::step): each quad spans its particle's
// move over the step, from the old end (vertices 0, 1) to the new (2, 3),
// Particle.LineHeight beyond each; drawn moved back by (1 - s) times that
// move. `line_height` is ParticleTuning::line_height.
void shift_streaks(const std::vector<LineVertex>& cur, float line_height, float s, std::vector<LineVertex>& out);

CameraState blend_camera(const CameraState& prev, const CameraState& cur, float s);

}  // namespace geow2

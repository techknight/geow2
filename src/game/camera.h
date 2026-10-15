#pragma once

#include "game/game_math.h"
#include "game/math.h"

namespace geow2 {

// Camera follow state: the centre (dat_821A1D80/84) and zoom (dat_821825EC,
// 1.0 at start).
struct CameraState {
  float x = 0.0f, y = 0.0f;
  float zoom = 1.0f;
};

// The in-game follow when the multiplayer flag dat_8219D275 is clear (the
// single-player case; sub_820366A0 at 0x82036A50..0x82036D38): the
// players' positions span a box (limited by the arena), the target is
// half of the box centre, the centre eases toward it at 0.15 and the zoom
// toward 1.0 at 0.075 per step.
void camera_follow_single(CameraState& c, const float* xs, const float* ys, int players, float half_x, float half_y);

// The view-projection the render pass builds (sub_82067A20): eye above the
// centre at height 512 looking down with up (0, -1, 0), fov 36 degrees
// times zoom over Render.Zoom (1.0), aspect 16:9, near 1, far 10000.
Mat4 camera_view_projection(const CameraState& c, const GameMath& gm);

}  // namespace geow2

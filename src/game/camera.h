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

// A slot's three smoothing stages of the camera follow (slot +0x14..+0x28).
struct CameraTrack {
  float s1x = 0, s1y = 0, s2x = 0, s2y = 0, s3x = 0, s3y = 0;
};

// The in-game follow with the multiplayer flag set (0x82036A50..
// 0x82036F44): each slot's stages ease after its position (its player, or
// its spawn point while it has none) at 0.2, 0.05 and 0.025 of the gap; the
// stages span a box; the centre eases toward half the box centre at 0.065
// and the zoom toward 1 + 0.4285714 times the box's fill of the arena (the
// larger of its width over 360 and its height over 240, less a margin) at
// 0.075.
void camera_follow_multi(CameraState& c, CameraTrack* tracks, const float* xs, const float* ys, int slots,
                         float half_x, float half_y);

// The view-projection the render pass builds (sub_82067A20): eye above the
// centre at height 512 looking down with up (0, -1, 0), fov 36 degrees
// times zoom over Render.Zoom (1.0), aspect 16:9, near 1, far 10000.
Mat4 camera_view_projection(const CameraState& c, const GameMath& gm);

// The follow basis of that view (rows x, y, z, eye; 0x82182618 while the
// debug camera is off).
Mat4 camera_basis(const CameraState& c);

// The view-projection from a basis and the follow zoom.
Mat4 camera_view_projection(const Mat4& basis, float zoom, const GameMath& gm);

// The debug free camera's flight (sub_82068CF8 from 0x82068DC4), one frame:
// moves and turns `basis` by pad 0's axes as sub_82037EA0 reads them (0..3
// sticks, 8/9 triggers, 10/11 bumpers), then rolls it toward world +z as
// up and normalises its rows.
void debug_camera_fly(Mat4& basis, const float* axis, const GameMath& gm);

// The same frame step out of a game (sub_820366A0 with sub_82033610
// false): the zoom eases to 1 and the centre decays toward the middle,
// slowly at first (the factor is the time out of a game, dat_8219D3CC,
// clamped to [0, 1]).
void camera_front_end(CameraState& c, float& idle, float step);

// The UI transform (sub_82067E40): UI units (640 x 360, y down) to clip
// space: M (x - 320, y - 180, z, via rows doubled and w = 2) times the view
// from (0, Ui.PositionY 0, Ui.PositionZ 170) looking down -z with up
// (0, -1, 0), times the 36 degree, 16:9 projection.
Mat4 ui_transform(const GameMath& gm);

}  // namespace geow2

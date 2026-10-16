#include "game/camera.h"

#include <cmath>

#include "game/rotation.h"
#include "game/fused.h"

namespace geow2 {
namespace {

// fsel(a, b, c): a >= 0 ? b : c (also for -0).
float fsel(float a, float b, float c) { return a >= 0.0f ? b : c; }

constexpr float kCentreRate = 0.15f;     // dat_82000D58
constexpr float kZoomRate = 0.075f;      // dat_82000D54
constexpr float kQuarter = 0.5f * 0.5f;  // dat_82000724 squared (f23)
constexpr float kEyeHeight = 512.0f;     // dat_82002DC0
constexpr float kDegToRad = 0.0174532924f;  // dat_82000F7C
constexpr float kFovDegrees = 36.0f;        // dat_820020AC
constexpr float kRenderZoom = 1.0f;         // Render.Zoom default
constexpr float kAspect = 1.77777779f;      // dat_82002DB0

}  // namespace

void camera_follow_single(CameraState& c, const float* xs, const float* ys, int players, float half_x, float half_y) {
  // Box starts as min = (+half), max = (-half) (0x820369F0..0x82036A1C) and
  // is grown with each player's three smoothing stages; with the flag
  // clear the stages are the position itself (0x82036B20..0x82036B34).
  float min_x = half_x, min_y = half_y, max_x = -half_x, max_y = -half_y;
  for (int i = 0; i < players; ++i) {
    for (int stage = 0; stage < 3; ++stage) {
      const float x = xs[i], y = ys[i];
      min_x = fsel(min_x - x, x, min_x);
      min_y = fsel(min_y - y, y, min_y);
      max_x = fsel(x - max_x, x, max_x);
      max_y = fsel(y - max_y, y, max_y);
    }
  }
  // 0x82036CE0..0x82036D38.
  const float tx = (max_x + min_x) * kQuarter;
  const float ty = (max_y + min_y) * kQuarter;
  c.zoom = fmadds(1.0f - c.zoom, kZoomRate, c.zoom);
  c.x = fmadds(tx - c.x, kCentreRate, c.x);
  c.y = fmadds(ty - c.y, kCentreRate, c.y);
}

void camera_follow_multi(CameraState& c, CameraTrack* tracks, const float* xs, const float* ys, int slots,
                         float half_x, float half_y) {
  constexpr float kStage1 = 0.2f, kStage2 = 0.05f, kStage3 = 0.025f;  // dat_82000D5C, D60, D64
  float min_x = half_x, min_y = half_y, max_x = -half_x, max_y = -half_y;
  for (int i = 0; i < slots; ++i) {
    CameraTrack& t = tracks[i];
    t.s1x = (xs[i] - t.s1x) * kStage1 + t.s1x;
    t.s1y = (ys[i] - t.s1y) * kStage1 + t.s1y;
    t.s2x = (t.s1x - t.s2x) * kStage2 + t.s2x;
    t.s2y = (t.s1y - t.s2y) * kStage2 + t.s2y;
    t.s3x = (t.s2x - t.s3x) * kStage3 + t.s3x;
    t.s3y = (t.s2y - t.s3y) * kStage3 + t.s3y;
    const float sx[3] = {t.s1x, t.s2x, t.s3x}, sy[3] = {t.s1y, t.s2y, t.s3y};
    for (int stage = 0; stage < 3; ++stage) {
      const float x = sx[stage], y = sy[stage];
      min_x = fsel(min_x - x, x, min_x);
      min_y = fsel(min_y - y, y, min_y);
      max_x = fsel(x - max_x, x, max_x);
      max_y = fsel(y - max_y, y, max_y);
    }
  }
  // 0x82036D48..0x82036E70: the box's fill of the arena less a margin
  // (18.25 by 12.5) over (half - 120) * 2 by (half - 80) * 2, each clamped
  // to [0, 1]; the largest zoom is the larger of 2 half_x * 0.002 and
  // 2 half_y * 0.0035714286.
  const float w = max_x - min_x, h = max_y - min_y;
  const float zx = (half_x * 2.0f) * 0.00200000009f;  // dat_82000D40
  const float zy = (half_y * 2.0f) * 0.00357142859f;  // dat_82000D44
  const float rx0 = (w - 18.25f) / ((half_x - 120.0f) * 2.0f);  // dat_82000D3C, dat_82000D50
  const float ry0 = (h - 12.5f) / ((half_y - 80.0f) * 2.0f);    // dat_82000D38, dat_82000D4C
  const float zmax = fsel(zx - zy, zx, zy);
  const float rx = fsel(rx0 - 1.0f, 1.0f, fsel(-rx0, 0.0f, rx0));
  const float ry = fsel(ry0 - 1.0f, 1.0f, fsel(-ry0, 0.0f, ry0));
  const float tx = ((max_x + min_x) * 0.5f) * 0.5f;
  const float ty = ((max_y + min_y) * 0.5f) * 0.5f;
  c.x = fmadds(tx - c.x, 0.0649999976f, c.x);  // dat_82000D48
  c.y = fmadds(ty - c.y, 0.0649999976f, c.y);
  const float fill = fsel(rx - ry, rx, ry);
  const float target = fmadds(zmax - 1.0f, fill, 1.0f);
  c.zoom = fmadds(target - c.zoom, kZoomRate, c.zoom);
}

void camera_front_end(CameraState& c, float& idle, float step) {
  idle = idle + step;
  c.zoom = fmadds(1.0f - c.zoom, kZoomRate, c.zoom);
  const float t = idle;
  const float k = fsel(t - 1.0f, 1.0f, fsel(-t, 0.0f, t));
  c.x = fmadds(-(k * c.x), kCentreRate, c.x);
  c.y = fmadds(-(k * c.y), kCentreRate, c.y);
}

Mat4 camera_view_projection(const CameraState& c, const GameMath& gm) {
  // The shake offset (dat_821A1D88/8C) is zero outside the shake effect.
  const Mat4 basis = look_at_basis({c.x, c.y, kEyeHeight, 1.0f}, {0.0f, 0.0f, -kEyeHeight, 0.0f}, {0.0f, -1.0f, 0.0f, 0.0f});
  const float fov = kDegToRad / (kRenderZoom / c.zoom) * kFovDegrees;
  return mul(inverse(basis), perspective(gm, fov, kAspect, 1.0f, 10000.0f));
}

Mat4 ui_transform(const GameMath& gm) {
  Mat4 m = Mat4::identity();
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 4; ++c) m.m[r * 4 + c] = m.m[r * 4 + c] * 2.0f;  // dat_820006FC
  m.m[12] = -640.0f;  // dat_82002DC4
  m.m[13] = -360.0f;  // dat_820022A0
  m.m[15] = 1.0f + 1.0f;
  // Direction normalise(0, TargetY - PositionY, -PositionZ) = (0, 0, -1).
  const float pz = 170.0f;  // Ui.PositionZ (0x821B4104, dat_82002EE8)
  const float len = std::sqrt(0.0f + pz * pz);
  const Mat4 basis = look_at_basis({0.0f, 0.0f, pz, 1.0f}, {0.0f, 0.0f, -pz / len, 0.0f}, {0.0f, -1.0f, 0.0f, 0.0f});
  const float fov = kDegToRad / kRenderZoom * kFovDegrees;
  return mul(m, mul(inverse(basis), perspective(gm, fov, kAspect, 1.0f, 10000.0f)));
}

}  // namespace geow2

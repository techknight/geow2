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

Mat4 camera_basis(const CameraState& c) {
  // The shake offset (dat_821A1D88/8C) is zero outside the shake effect.
  return look_at_basis({c.x, c.y, kEyeHeight, 1.0f}, {0.0f, 0.0f, -kEyeHeight, 0.0f}, {0.0f, -1.0f, 0.0f, 0.0f});
}

Mat4 camera_view_projection(const Mat4& basis, float zoom, const GameMath& gm) {
  const float fov = kDegToRad / (kRenderZoom / zoom) * kFovDegrees;
  return mul(inverse(basis), perspective(gm, fov, kAspect, 1.0f, 10000.0f));
}

Mat4 camera_view_projection(const CameraState& c, const GameMath& gm) {
  return camera_view_projection(camera_basis(c), c.zoom, gm);
}

namespace {

// sub_820688E8: a radial dead zone of 0.25 (dat_82000720) rescaled to
// 0..1 (1.3333334, dat_82002DE8); the length is clamped to [0, 1] with fsel.
struct Stick {
  float x, y;
};

Stick debug_dead_zone(float x, float y) {
  const float m = std::sqrt(x * x + y * y);
  const float inv = 1.0f / m;
  const double lo = -m >= 0.0f ? 0.0 : double(m);
  const float c = float(m - 1.0f >= 0.0f ? 1.0 : lo);
  if (!(c >= 0.25f)) return {0.0f, 0.0f};
  const float s = (c - 0.25f) * 1.3333334f;
  return {s * (inv * x), s * (inv * y)};
}

// fsel(v, v * v, -(v * v)): the square with v's sign.
float signed_square(float v) {
  const float sq = v * v;
  return v >= 0.0f ? sq : -sq;
}

float row_dot(const float* u, const float* q) {
  // ((w + z) + y) + x.
  return ((u[3] * q[3] + u[2] * q[2]) + u[1] * q[1]) + u[0] * q[0];
}

}  // namespace

void debug_camera_fly(Mat4& basis, const float* axis, const GameMath& gm) {
  float* b = basis.m;
  // Sticks (0x82068E1C..0x82068E70): up is positive after the negation.
  const Stick left = debug_dead_zone(axis[0], axis[1]);
  const Stick right = debug_dead_zone(axis[2], axis[3]);
  const float lx = left.x, ly = -left.y, rx = right.x, ry = -right.y;
  const float lt = axis[8], rt = axis[9];
  const bool lb = axis[10] > 0.5f, rb = axis[11] > 0.5f;  // dat_82000724

  // Translation along the old rows (0x82068F30..0x82069080): speed 10
  // (dat_8200070C), RB ten times, LB a tenth (dat_82000714); sticks 0.75
  // (dat_82000718), triggers 0.5.
  const float speed = ((rb ? 10.0f : 1.0f) * (lb ? 0.1f : 1.0f)) * 10.0f;
  const float sx = signed_square(rx), sy = signed_square(ry), st = signed_square(rt - lt);
  float p[4];
  for (int c = 0; c < 4; ++c)
    p[c] = ((b[12 + c] + ((b[c] * sx) * 0.75f) * speed) + ((b[4 + c] * st) * 0.5f) * speed) +
           ((b[8 + c] * sy) * 0.75f) * speed;

  // Rotations (0x82069084..0x82069188): pitch about row 0, yaw about row
  // 1, both from the old rows, 0.075 rad (dat_82000D54) at full stick, a
  // quarter with LB; M2 = (M * yaw) * pitch.
  Mat4 m = basis;
  m.m[12] = m.m[13] = m.m[14] = 0.0f;
  m.m[15] = 1.0f;
  const float k = lb ? 0.25f : 1.0f;
  const Mat4 pitch = rotation_axis(gm, (signed_square(ly) * k) * 0.075f, m.m[0], m.m[1], m.m[2]);
  const Mat4 yaw = rotation_axis(gm, (signed_square(lx) * k) * 0.075f, m.m[4], m.m[5], m.m[6]);
  m = mul(mul(m, yaw), pitch);

  // Roll toward world +z as up (0x8206918C..0x820693B8).
  const float* q2 = &m.m[8];
  const float d = ((q2[3] * 0.0f + q2[2]) + q2[1] * 0.0f) + q2[0] * 0.0f;
  const float v[4] = {-(d * q2[0]), -(q2[1] * d), 1.0f - q2[2] * d, -(q2[3] * d)};
  const float inv_v = 1.0f / std::sqrt(((v[3] * v[3] + v[2] * v[2]) + v[1] * v[1]) + v[0] * v[0]);
  const float u[4] = {v[0] * inv_v, v[1] * inv_v, v[2] * inv_v, v[3] * inv_v};
  const float e = row_dot(u, &m.m[4]);
  // Clamped to [-1, 1] with fsel (dat_82000C88, 0x82000C80, 0x82000C78).
  const double ce = -1.0f - e >= 0.0f ? -1.0 : double(e);
  const float a = float(gm.acos(double(float(e - 1.0f >= 0.0f ? 1.0 : ce))));
  if (a > 0.0005f) {  // dat_82002DFC
    const float s = row_dot(u, &m.m[0]);
    const float g = s < 0.0f ? 1.0f : (s > 0.0f ? -1.0f : 0.0f);
    const float r = g * a;
    // Clamped to [-0.075, 0.075] (dat_82002DF8, 0x82002DF0, 0x820011A8).
    const double cr = -0.075f - r >= 0.0f ? double(-0.075f) : double(r);
    const float roll = float(r - 0.075f >= 0.0f ? double(0.075f) : cr);
    m = mul(m, rotation_axis(gm, roll, q2[0], q2[1], q2[2]));
  }

  // Each row normalised (0x820693BC..0x820694A4), the new eye as row 3.
  for (int row = 0; row < 3; ++row) {
    const float* q = &m.m[row * 4];
    const float f = 1.0f / std::sqrt(((q[3] * q[3] + q[2] * q[2]) + q[1] * q[1]) + q[0] * q[0]);
    for (int c = 0; c < 4; ++c) b[row * 4 + c] = q[c] * f;
  }
  for (int c = 0; c < 4; ++c) b[12 + c] = p[c];
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

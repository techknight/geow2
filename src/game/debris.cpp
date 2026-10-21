#include "game/debris.h"
#include "game/fused.h"

#include <cmath>

namespace geow2 {
namespace {

constexpr float kTwoPow32 = 2.32830644e-10f;  // dat_820006E4
constexpr double kBounce = 0.75;              // dat_82002780 / dat_82002A68 (f64, +/-)

// Wall handling of one end (0x82074C80..0x82074DC4): a coordinate past a
// wall is mirrored back inside (computed in double, then rounded), and
// once either coordinate of the end is outside, both velocity components
// are scaled: by -0.75 for the coordinate that crossed, 0.75 otherwise.
struct Bounced {
  float x, y, vx, vy;
};

Bounced bounce(float x, float y, float vx, float vy, float hx, float hy) {
  const float in_right = hx - x, in_top = hy - y;     // >= 0 when inside
  const float in_left = hx + x, in_bottom = hy + y;
  // Mirrored positions (fsel chains): first the low wall, then the high.
  const double mx = in_left >= 0.0f ? double(x) : double(-hx) - double(in_left);
  const double my = in_bottom >= 0.0f ? double(y) : double(-hy) - double(in_bottom);
  const double rx = in_right >= 0.0f ? mx : double(hx) + double(in_right);
  const double ry = in_top >= 0.0f ? my : double(hy) + double(in_top);
  // Velocity factors per axis.
  const double fx = in_left >= 0.0f ? (in_right >= 0.0f ? kBounce : -kBounce) : -kBounce;
  const double fy = in_bottom >= 0.0f ? (in_top >= 0.0f ? kBounce : -kBounce) : -kBounce;
  // Any crossing: the x test's distance when x is outside, else y's.
  const double dist_x = in_left >= 0.0f ? double(in_right) : double(in_left);
  const double dist_y = in_bottom >= 0.0f ? double(in_top) : double(in_bottom);
  const double inside = dist_x >= 0.0 ? dist_y : dist_x;
  Bounced b;
  b.x = float(rx);
  b.y = float(ry);
  b.vx = inside >= 0.0 ? vx : float(fx * double(vx));
  b.vy = inside >= 0.0 ? vy : float(fy * double(vy));
  return b;
}

}  // namespace

void Debris::add(const Vec4& p0, const Vec4& v0, const Vec4& p1, const Vec4& v1, const float colour[4], GameRng& rng) {
  if (pieces_.size() >= kCapacity) return;
  DebrisPiece d;
  d.p0 = p0;
  d.v0 = v0;
  d.p1 = p1;
  d.v1 = v1;
  d.q0 = p0;
  d.q1 = p1;
  const float dx = p1.x - p0.x, dw = p1.w - p0.w, dz = p1.z - p0.z, dy = p1.y - p0.y;
  d.length = std::sqrt(((dx * dx + dw * dw) + dz * dz) + dy * dy);
  // Life: 0.75 to 1.25 seconds (dat_82000724, dat_82000718).
  d.life = fmadds(float(rng.next()) * kTwoPow32, 0.5f, 0.75f);
  d.life_total = d.life;
  for (int i = 0; i < 4; ++i) d.colour[i] = colour[i];
  pieces_.push_back(d);
}

void Debris::smash(Effects& fx, GameRng& rng, float speed, const std::vector<ModelLine>& lines, const Mat4& m,
                   float px, float py, float killer_vx, float killer_vy, const float colour[4]) {
  if (lines.empty()) return;
  // The killer's velocity times 0.05 (dat_82000D60) pushes the pieces; a
  // slow one (that push under 10, dat_8200070C) is replaced by twice the
  // way from the impact to the model's origin (matrix row 3).
  float push[4] = {killer_vx * 0.05f, killer_vy * 0.05f, 0.0f, 0.0f};
  if (std::sqrt(push[1] * push[1] + push[0] * push[0]) < 10.0f) {
    push[2] = m.m[14] * 2.0f;
    push[0] = (m.m[12] - px) * 2.0f;
    push[3] = (m.m[15] - 1.0f) * 2.0f;
    push[1] = (m.m[13] - py) * 2.0f;
  }
  // The far ends get three quarters of it, the other way (dat_82000718).
  const float back[4] = {push[0] * 0.75f, push[1] * 0.75f, push[2] * 0.75f, push[3] * 0.75f};
  for (const ModelLine& l : lines) {
    const Vec4 a = mul(l.p0, m), b = mul(l.p1, m);
    float r[4];
    fx.random_cube(0.5f, r);
    // Each end flies along its way from the impact (plus a random kick)
    // doubled, at a speed growing with the square of that length.
    const float ax = ((r[0] + (a.x - px)) * 2.0f) + push[0];
    const float ay = ((r[1] + (a.y - py)) * 2.0f) + push[1];
    const float az = ((r[2] + a.z) * 2.0f) + push[2];
    const float aw = ((r[3] + (a.w - 1.0f)) * 2.0f) + push[3];
    float r2[4];
    fx.random_cube(0.5f, r2);
    const float sa = (std::sqrt(((aw * aw + az * az) + ay * ay) + ax * ax) * speed) * 0.2f;  // dat_82000D5C
    const float bx = (((b.x - px) + r2[0]) * 2.0f) - back[0];
    const float by = ((r2[1] + (b.y - py)) * 2.0f) - back[1];
    const float bz = ((r2[2] + b.z) * 2.0f) - back[2];
    const float bw = ((r2[3] + (b.w - 1.0f)) * 2.0f) - back[3];
    const Vec4 va{sa * ax, ay * sa, (sa * az) * 0.2f, aw * sa};
    const float c[4] = {l.r * colour[0], l.g * colour[1], l.b * colour[2], l.a * colour[3]};
    const float sb = (std::sqrt(((bw * bw + bz * bz) + by * by) + bx * bx) * speed) * 0.2f;
    const Vec4 vb{sb * bx, by * sb, (sb * bz) * 0.2f, bw * sb};
    add(a, va, b, vb, c, rng);
  }
}

void Debris::step(const SmashTuning& t, float dt, float hx, float hy) {
  for (DebrisPiece& d : pieces_) {
    d.q0 = d.p0;
    d.q1 = d.p1;
  }
  size_t end = pieces_.size();
  for (size_t i = 0; i < end; ++i) {
    DebrisPiece& d = pieces_[i];
    d.life = d.life - dt;
    if (!(d.life > 0.0f)) {
      // The last piece takes this one's place and waits for the next step.
      --end;
      d = pieces_[end];
      continue;
    }
    // Move both ends.
    const float x0 = dt * d.v0.x + d.p0.x, y0 = dt * d.v0.y + d.p0.y;
    const float z0 = d.p0.z + d.v0.z * dt, w0 = d.p0.w + d.v0.w * dt;
    const float x1 = d.p1.x + d.v1.x * dt, y1 = d.v1.y * dt + d.p1.y;
    const float z1 = d.p1.z + d.v1.z * dt, w1 = d.v1.w * dt + d.p1.w;
    const Bounced e0 = bounce(x0, y0, d.v0.x, d.v0.y, hx, hy);
    const Bounced e1 = bounce(x1, y1, d.v1.x, d.v1.y, hx, hy);
    // Damped velocities (Smash.Damp).
    const float k = t.damp;
    const float v0x = k * e0.vx, v0y = k * e0.vy, v0z = d.v0.z * k, v0w = d.v0.w * k;
    const float v1x = e1.vx * k, v1y = e1.vy * k, v1z = d.v1.z * k, v1w = d.v1.w * k;
    // The spring toward the rest length (Smash.SpringStrength), along the
    // unit direction from end 0 to end 1.
    const float dx = e1.x - e0.x, dy = e1.y - e0.y, dz = z1 - z0, dw = w1 - w0;
    const float dist = std::sqrt((((dw * dw + dz * dz) + dy * dy) + dx * dx));
    const float inv = 1.0f / dist;
    const float s = (d.length - dist) * t.spring;
    const float nx = inv * dx, ny = inv * dy, nz = inv * dz, nw = inv * dw;
    d.v0 = {v0x - s * nx, v0y - ny * s, v0z - nz * s, v0w - nw * s};
    d.v1 = {s * nx + v1x, ny * s + v1y, nz * s + v1z, nw * s + v1w};
    // Both ends set at the rest length either side of the middle.
    const float cx = (e1.x + e0.x) * 0.5f, cy = (e1.y + e0.y) * 0.5f;  // dat_82000724
    const float cz = (z1 + z0) * 0.5f, cw = (w1 + w0) * 0.5f;
    const float hx2 = (d.length * nx) * 0.5f, hy2 = (ny * d.length) * 0.5f;
    const float hz2 = (nz * d.length) * 0.5f, hw2 = (nw * d.length) * 0.5f;
    d.p0 = {cx - hx2, cy - hy2, cz - hz2, cw - hw2};
    d.p1 = {hx2 + cx, hy2 + cy, hz2 + cz, hw2 + cw};
  }
  pieces_.resize(end);
}

// A piece's fade and its ends' colours (sub_82074600): k = clamp(life, 0,
// 1) / 2, and the end nearer the camera (higher z) brighter, 3 +/- 0.075
// min(|dz|, 10) (dat_820006F8, dat_82000D54, dat_8200070C).
static float piece_colours(const DebrisPiece& d, float c0[4], float c1[4]) {
  float life = d.life <= 0.0f ? 0.0f : d.life;
  if (life - 1.0f >= 0.0f) life = 1.0f;
  const float k = life * 0.5f;
  const float dz = d.p1.z - d.p0.z;
  float m = std::fabs(dz);
  if (10.0f - m < 0.0f) m = 10.0f;
  m = m * 0.075f;
  const float hi = m + 3.0f, lo = 3.0f - m;
  const float k1 = (dz >= 0.0f ? hi : lo) * k;  // end 1
  const float k0 = (dz >= 0.0f ? lo : hi) * k;  // end 0
  const float* c = d.colour;
  c0[0] = c[0] * k0, c0[1] = c[1] * k0, c0[2] = c[2] * k0, c0[3] = k0 * c[3];
  c1[0] = c[0] * k1, c1[1] = c[1] * k1, c1[2] = c[2] * k1, c1[3] = k1 * c[3];
  return k;
}

void Debris::draw(LineBatch& out, std::vector<LightVertex>& lights) const {
  for (const DebrisPiece& d : pieces_) {
    float c0[4], c1[4];
    const float k = piece_colours(d, c0, c1);
    emit_line2(out, kLineWidthUnit, d.p0, d.p1, c0, c1);
    // A light at the middle, size twice the length, colour * k * 18
    // (dat_82002728).
    const float g = k * 18.0f;
    const float* c = d.colour;
    const float lc[3] = {c[0] * g, c[1] * g, c[2] * g};
    emit_light(lights, d.length * 2.0f, (d.p1.x + d.p0.x) * 0.5f, (d.p1.y + d.p0.y) * 0.5f, lc);
  }
}

void Debris::draw_between(LineBatch& out, float s) const {
  auto lerp = [s](const Vec4& a, const Vec4& b) {
    return Vec4{a.x + (b.x - a.x) * s, a.y + (b.y - a.y) * s, a.z + (b.z - a.z) * s, a.w + (b.w - a.w) * s};
  };
  for (const DebrisPiece& d : pieces_) {
    float c0[4], c1[4];
    piece_colours(d, c0, c1);
    emit_line2(out, kLineWidthUnit, lerp(d.q0, d.p0), lerp(d.q1, d.p1), c0, c1);
  }
}

}  // namespace geow2

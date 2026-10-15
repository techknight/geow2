#include "game/rotation.h"

#include <cmath>

namespace geow2 {

Mat4 rotation_euler(const GameMath& gm, float a, float b, float c) {
  const float sa = gm.sinf(a), ca = gm.cosf(a), sb = gm.sinf(b), cb = gm.cosf(b), sc = gm.sinf(c), cc = gm.cosf(c);
  Mat4 r = Mat4::identity();
  r.m[0] = cc * cb;
  r.m[1] = sc * cb;
  r.m[2] = -sb;
  // fmsubs/fmadds at 0x820319D4..0x820319FC.
  r.m[4] = std::fmaf(sb * sa, cc, -(sc * ca));
  r.m[5] = std::fmaf(sb * sa, sc, cc * ca);
  r.m[6] = cb * sa;
  r.m[8] = std::fmaf(sb * ca, cc, sc * sa);
  r.m[9] = std::fmaf(sb * ca, sc, -(cc * sa));
  r.m[10] = cb * ca;
  return r;
}

Mat4 rotation_axis(const GameMath& gm, float angle, float x, float y, float z) {
  // All nine elements are fused (0x820416A0..0x82041768).
  const float c = gm.cosf(angle), s = gm.sinf(angle), t = 1.0f - c;
  Mat4 r = Mat4::identity();
  r.m[0] = std::fmaf(x * x, t, c);
  r.m[1] = std::fmaf(t * x, y, z * s);
  r.m[2] = std::fmaf(z * t, x, -(s * y));
  r.m[4] = std::fmaf(t * x, y, -(z * s));
  r.m[5] = std::fmaf(y * y, t, c);
  r.m[6] = std::fmaf(z * t, y, x * s);
  r.m[8] = std::fmaf(z * t, x, s * y);
  r.m[9] = std::fmaf(z * t, y, -(x * s));
  r.m[10] = std::fmaf(z * z, t, c);
  return r;
}

Mat4 placement_style_none(const GameMath& gm, float x, float y, float heading) {
  Mat4 m = rotation_axis(gm, heading, 0.0f, 0.0f, 1.0f);
  m.m[12] += x;  // sub_82040FF0 with (x, y, 0, 0)
  m.m[13] += y;
  return m;
}

Mat4 look_at_basis(Vec4 p, Vec4 f, Vec4 u) {
  // Operation order from the disassembly of sub_82047180 (0x82047180..0x820472F0).
  const float inv_f = 1.0f / std::sqrt(((f.w * f.w + f.z * f.z) + f.x * f.x) + f.y * f.y);
  const float zx = f.x * inv_f, zy = f.y * inv_f, zz = f.z * inv_f, zw = f.w * inv_f;
  const float d = ((u.w * zw + u.z * zz) + u.y * zy) + u.x * zx;
  float yx = u.x - zx * d, yy = u.y - zy * d, yz = u.z - zz * d, yw = u.w - zw * d;
  const float inv_y = 1.0f / std::sqrt(((yw * yw + yz * yz) + yy * yy) + yx * yx);
  yx *= inv_y;
  yy *= inv_y;
  yz *= inv_y;
  yw *= inv_y;
  const float xz = std::fmaf(zy, yx, -(yy * zx));
  const float xy = std::fmaf(yz, zx, -(zz * yx));
  const float xx = std::fmaf(yy, zz, -(yz * zy));
  const float inv_x = 1.0f / std::sqrt((xz * xz + xy * xy) + xx * xx);
  Mat4 m;
  m.m[0] = inv_x * xx;
  m.m[1] = xy * inv_x;
  m.m[2] = xz * inv_x;
  m.m[3] = inv_x * 0.0f;
  m.m[4] = yx;
  m.m[5] = yy;
  m.m[6] = yz;
  m.m[7] = yw;
  m.m[8] = zx;
  m.m[9] = zy;
  m.m[10] = zz;
  m.m[11] = zw;
  m.m[12] = p.x;
  m.m[13] = p.y;
  m.m[14] = p.z;
  m.m[15] = p.w;
  return m;
}

Mat4 inverse(const Mat4& a) {
  // Cofactor expansion in double, rounded once per element.
  const float* m = a.m;
  double inv[16];
  inv[0] = double(m[5]) * m[10] * m[15] - double(m[5]) * m[11] * m[14] - double(m[9]) * m[6] * m[15] +
           double(m[9]) * m[7] * m[14] + double(m[13]) * m[6] * m[11] - double(m[13]) * m[7] * m[10];
  inv[4] = -double(m[4]) * m[10] * m[15] + double(m[4]) * m[11] * m[14] + double(m[8]) * m[6] * m[15] -
           double(m[8]) * m[7] * m[14] - double(m[12]) * m[6] * m[11] + double(m[12]) * m[7] * m[10];
  inv[8] = double(m[4]) * m[9] * m[15] - double(m[4]) * m[11] * m[13] - double(m[8]) * m[5] * m[15] +
           double(m[8]) * m[7] * m[13] + double(m[12]) * m[5] * m[11] - double(m[12]) * m[7] * m[9];
  inv[12] = -double(m[4]) * m[9] * m[14] + double(m[4]) * m[10] * m[13] + double(m[8]) * m[5] * m[14] -
            double(m[8]) * m[6] * m[13] - double(m[12]) * m[5] * m[10] + double(m[12]) * m[6] * m[9];
  inv[1] = -double(m[1]) * m[10] * m[15] + double(m[1]) * m[11] * m[14] + double(m[9]) * m[2] * m[15] -
           double(m[9]) * m[3] * m[14] - double(m[13]) * m[2] * m[11] + double(m[13]) * m[3] * m[10];
  inv[5] = double(m[0]) * m[10] * m[15] - double(m[0]) * m[11] * m[14] - double(m[8]) * m[2] * m[15] +
           double(m[8]) * m[3] * m[14] + double(m[12]) * m[2] * m[11] - double(m[12]) * m[3] * m[10];
  inv[9] = -double(m[0]) * m[9] * m[15] + double(m[0]) * m[11] * m[13] + double(m[8]) * m[1] * m[15] -
           double(m[8]) * m[3] * m[13] - double(m[12]) * m[1] * m[11] + double(m[12]) * m[3] * m[9];
  inv[13] = double(m[0]) * m[9] * m[14] - double(m[0]) * m[10] * m[13] - double(m[8]) * m[1] * m[14] +
            double(m[8]) * m[2] * m[13] + double(m[12]) * m[1] * m[10] - double(m[12]) * m[2] * m[9];
  inv[2] = double(m[1]) * m[6] * m[15] - double(m[1]) * m[7] * m[14] - double(m[5]) * m[2] * m[15] +
           double(m[5]) * m[3] * m[14] + double(m[13]) * m[2] * m[7] - double(m[13]) * m[3] * m[6];
  inv[6] = -double(m[0]) * m[6] * m[15] + double(m[0]) * m[7] * m[14] + double(m[4]) * m[2] * m[15] -
           double(m[4]) * m[3] * m[14] - double(m[12]) * m[2] * m[7] + double(m[12]) * m[3] * m[6];
  inv[10] = double(m[0]) * m[5] * m[15] - double(m[0]) * m[7] * m[13] - double(m[4]) * m[1] * m[15] +
            double(m[4]) * m[3] * m[13] + double(m[12]) * m[1] * m[7] - double(m[12]) * m[3] * m[5];
  inv[14] = -double(m[0]) * m[5] * m[14] + double(m[0]) * m[6] * m[13] + double(m[4]) * m[1] * m[14] -
            double(m[4]) * m[2] * m[13] - double(m[12]) * m[1] * m[6] + double(m[12]) * m[2] * m[5];
  inv[3] = -double(m[1]) * m[6] * m[11] + double(m[1]) * m[7] * m[10] + double(m[5]) * m[2] * m[11] -
           double(m[5]) * m[3] * m[10] - double(m[9]) * m[2] * m[7] + double(m[9]) * m[3] * m[6];
  inv[7] = double(m[0]) * m[6] * m[11] - double(m[0]) * m[7] * m[10] - double(m[4]) * m[2] * m[11] +
           double(m[4]) * m[3] * m[10] + double(m[8]) * m[2] * m[7] - double(m[8]) * m[3] * m[6];
  inv[11] = -double(m[0]) * m[5] * m[11] + double(m[0]) * m[7] * m[9] + double(m[4]) * m[1] * m[11] -
            double(m[4]) * m[3] * m[9] - double(m[8]) * m[1] * m[7] + double(m[8]) * m[3] * m[5];
  inv[15] = double(m[0]) * m[5] * m[10] - double(m[0]) * m[6] * m[9] - double(m[4]) * m[1] * m[10] +
            double(m[4]) * m[2] * m[9] + double(m[8]) * m[1] * m[6] - double(m[8]) * m[2] * m[5];
  const double det = double(m[0]) * inv[0] + double(m[1]) * inv[4] + double(m[2]) * inv[8] + double(m[3]) * inv[12];
  Mat4 r{};
  if (det == 0.0) return r;
  for (int i = 0; i < 16; ++i) r.m[i] = float(inv[i] / det);
  return r;
}

Mat4 perspective(const GameMath& gm, float fov, float aspect, float near_z, float far_z) {
  // sub_8206A300: fdivs throughout.
  const float t = float(gm.tan(double(fov * 0.5f)));
  const float ys = 1.0f / t;
  const float q = far_z / (far_z - near_z);
  Mat4 m{};
  m.m[0] = ys / aspect;
  m.m[5] = ys;
  m.m[10] = q;
  m.m[11] = 1.0f;
  m.m[14] = -(q * near_z);
  return m;
}

}  // namespace geow2

#pragma once

// Row-vector 4x4 matrices as the game uses them (v' = v * M, translation
// in row 3): sub_82030338 (vector times matrix), sub_82041CA0 (A * B).

namespace geow2 {

struct Vec4 {
  float x = 0, y = 0, z = 0, w = 0;
};

struct Mat4 {
  float m[16];  // row-major: m[row * 4 + col]

  static Mat4 identity() {
    Mat4 r{};
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
  }
};

// sub_82030338: row vector times matrix.
inline Vec4 mul(const Vec4& v, const Mat4& a) {
  const float* m = a.m;
  return {v.x * m[0] + v.y * m[4] + v.z * m[8] + v.w * m[12], v.x * m[1] + v.y * m[5] + v.z * m[9] + v.w * m[13],
          v.x * m[2] + v.y * m[6] + v.z * m[10] + v.w * m[14], v.x * m[3] + v.y * m[7] + v.z * m[11] + v.w * m[15]};
}

// sub_82041CA0: out = A * B (each row of A transformed by B).
inline Mat4 mul(const Mat4& a, const Mat4& b) {
  Mat4 r{};
  for (int row = 0; row < 4; ++row) {
    const Vec4 v = mul(Vec4{a.m[row * 4], a.m[row * 4 + 1], a.m[row * 4 + 2], a.m[row * 4 + 3]}, b);
    r.m[row * 4] = v.x;
    r.m[row * 4 + 1] = v.y;
    r.m[row * 4 + 2] = v.z;
    r.m[row * 4 + 3] = v.w;
  }
  return r;
}

}  // namespace geow2

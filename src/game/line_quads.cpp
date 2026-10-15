#include "game/line_quads.h"

#include <cmath>

namespace geow2 {
namespace {

constexpr float kColourScale = 18.0f;                    // dat_82002728
constexpr double kLengthEpsilon = 9.999999747378752e-06;  // dat_82002760 (f64; 1e-5 as a float)
constexpr float kSideMinus = -1.0f;                       // dat_82000C88
constexpr float kSidePlus = 1.0f;                         // dat_820006C4
constexpr float kTipAlpha = 0.0f;                         // dat_820006CC

}  // namespace

void emit_line(LineBatch& out, float width, float cap_size, const Vec4& p0, const Vec4& p1, const float colour[4]) {
  // Colour: (r * a) * 18 per channel, in single precision (fmuls).
  const float a = colour[3];
  const float r = (a * colour[0]) * kColourScale;
  const float g = (colour[1] * a) * kColourScale;
  const float b = (colour[2] * a) * kColourScale;

  const float dx = p1.x - p0.x, dy = p1.y - p0.y, dz = p1.z - p0.z;
  // Lengths in double: the 2D one for the perpendicular, the 3D one for
  // the cap direction. The original takes frsqrte (a hardware estimate) of
  // both; the port uses the exact reciprocal square root (DIFFERENCES.md).
  const double x2 = double(dx) * dx, y2 = double(dy) * dy;
  const double l3 = (std::fma(double(dz), double(dz), x2) + y2) + kLengthEpsilon;
  const double l2 = (x2 + y2) + kLengthEpsilon;
  const double i2 = 1.0 / std::sqrt(l2), i3 = 1.0 / std::sqrt(l3);
  const double c = i3 * double(cap_size);
  const double px = i2 * dy, py = -(i2 * dx);  // the 2D perpendicular
  const double cx = c * dx, cy = c * dy, cz = c * dz;
  const double w = width;
  const double x0 = p0.x, y0 = p0.y, z0 = p0.z, x1 = p1.x, y1 = p1.y, z1 = p1.z;
  auto f = [](double v) { return float(v); };  // frsp
  auto vertex = [&](double x, double y, double z, float side, float alpha) {
    return LineVertex{f(x), f(y), f(z), side, r, g, b, alpha};
  };

  // Body quad: both ends offset by +/- width along the perpendicular.
  out.body.push_back(vertex(std::fma(-w, px, x0), std::fma(-w, py, y0), z0, kSideMinus, 1.0f));
  out.body.push_back(vertex(std::fma(w, px, x0), std::fma(w, py, y0), z0, kSidePlus, 1.0f));
  out.body.push_back(vertex(std::fma(w, px, x1), std::fma(w, py, y1), z1, kSidePlus, 1.0f));
  out.body.push_back(vertex(std::fma(-w, px, x1), std::fma(-w, py, y1), z1, kSideMinus, 1.0f));

  // End caps: from each end outward along the 3D direction, width * cap
  // long, fading to alpha 0 at the tip. The tip coordinates chain a second
  // fused operation on the unrounded first, except the p1 '+' tip, which
  // adds the two directions first.
  out.caps.push_back(vertex(std::fma(-w, px, x0), std::fma(-w, py, y0), z0, kSideMinus, 1.0f));
  out.caps.push_back(vertex(std::fma(w, px, x0), std::fma(w, py, y0), z0, kSidePlus, 1.0f));
  out.caps.push_back(vertex(std::fma(-w, cx, std::fma(w, px, x0)), std::fma(-w, cy, std::fma(w, py, y0)),
                            std::fma(-w, cz, z0), kSidePlus, kTipAlpha));
  out.caps.push_back(vertex(std::fma(-w, cx, std::fma(-w, px, x0)), std::fma(-w, cy, std::fma(-w, py, y0)),
                            std::fma(-w, cz, z0), kSideMinus, kTipAlpha));
  out.caps.push_back(vertex(std::fma(-w, px, x1), std::fma(-w, py, y1), z1, kSideMinus, 1.0f));
  out.caps.push_back(vertex(std::fma(w, px, x1), std::fma(w, py, y1), z1, kSidePlus, 1.0f));
  out.caps.push_back(
      vertex(std::fma(px + cx, w, x1), std::fma(py + cy, w, y1), std::fma(w, cz, z1), kSidePlus, kTipAlpha));
  out.caps.push_back(vertex(std::fma(w, cx, std::fma(-w, px, x1)), std::fma(w, cy, std::fma(-w, py, y1)),
                            std::fma(w, cz, z1), kSideMinus, kTipAlpha));
}

}  // namespace geow2

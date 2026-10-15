#include "game/light_map.h"

#include <cmath>

namespace geow2 {
namespace {

constexpr double kLengthEpsilon = 9.999999747378752e-06;  // dat_82002760 (f64)
constexpr float kSideMinus = -1.125f;                     // dat_82002768
constexpr float kSidePlus = 1.125f;                       // dat_8200276C

// One line quad from p to q (current-buffer positions read as doubles):
// n = (1 / sqrt(len2)) * w, then each coordinate one double add or
// subtract rounded to float. The original's frsqrte is an exact 1/sqrt
// here (DIFFERENCES.md).
void quad(std::vector<GridVertex>& out, Vec2 p, Vec2 q, Vec2 uvp, Vec2 uvq, double len2, double w, float z,
          float alpha) {
  const double px = p.x, py = p.y, qx = q.x, qy = q.y;
  const double dx = qx - px, dy = qy - py;
  const double n = (1.0 / std::sqrt(len2 + kLengthEpsilon)) * w;
  const double ny = n * dy, nx = n * dx;
  out.push_back({float(px - ny), float(nx + py), z, uvp.x, uvp.y, alpha, kSideMinus});
  out.push_back({float(ny + px), float(py - nx), z, uvp.x, uvp.y, alpha, kSidePlus});
  out.push_back({float(ny + qx), float(qy - nx), z, uvq.x, uvq.y, alpha, kSidePlus});
  out.push_back({float(qx - ny), float(nx + qy), z, uvq.x, uvq.y, alpha, kSideMinus});
}

// Squared lengths as the two segment kinds compute them (the fused
// multiply-add squares a different component).
double len2_dy_fused(Vec2 p, Vec2 q) {
  const double dx = double(q.x) - p.x, dy = double(q.y) - p.y;
  return std::fma(dy, dy, dx * dx);
}

double len2_dx_fused(Vec2 p, Vec2 q) {
  const double dx = double(q.x) - p.x, dy = double(q.y) - p.y;
  return std::fma(dx, dx, dy * dy);
}

// sub_82067850(x, y, sigma) in double: a normal density.
double gauss(double x, double y, double sigma) {
  constexpr double kTwo = 2.0;                   // dat_82001180 (f64)
  constexpr double kTwoPi = 6.2831854820251465;  // dat_82002DA8 (f64)
  const double s2 = sigma * sigma;
  return std::exp(-(std::fma(x, x, y * y) / (s2 * kTwo))) / std::sqrt(s2 * kTwoPi);
}

}  // namespace

void grid_lines(const Grid& g, const GridTuning& t, float fade, std::vector<GridVertex>& out) {
  out.clear();
  // Width: fmuls(Render.Grid.LineWidth, fade); alpha levels: Alpha_k *
  // Brightness (sub_8205C8F8).
  const double w = t.line_width * fade;
  std::array<float, 8> alpha{};
  for (int k = 0; k < 8; ++k) alpha[size_t(k)] = t.alpha[size_t(k)] * t.brightness;
  auto at = [&g](int i, int j) { return g.previous_position(i, j); };
  // Row 1 first writes the verticals from row 0.
  for (int j = 1; j < Grid::kCols - 1; ++j) {
    const Vec2 p = at(0, j), q = at(1, j);
    quad(out, p, q, g.uv(0, j), g.uv(1, j), len2_dy_fused(p, q), w, t.depth, alpha[size_t(j & 7)]);
  }
  for (int i = 1; i < Grid::kRows - 1; ++i) {
    const float row_alpha = alpha[size_t(i & 7)];
    {
      const Vec2 p = at(i, 0), q = at(i, 1);
      quad(out, p, q, g.uv(i, 0), g.uv(i, 1), len2_dy_fused(p, q), w, t.depth, row_alpha);
    }
    for (int j = 1; j < Grid::kCols - 1; ++j) {
      const Vec2 p = at(i, j), right = at(i, j + 1), below = at(i + 1, j);
      quad(out, p, right, g.uv(i, j), g.uv(i, j + 1), len2_dy_fused(p, right), w, t.depth, row_alpha);
      quad(out, p, below, g.uv(i, j), g.uv(i + 1, j), len2_dx_fused(p, below), w, t.depth, alpha[size_t(j & 7)]);
    }
  }
}

void grid_warp_mesh(const Grid& g, const GridTuning& t, std::vector<GridVertex>& out) {
  out.clear();
  for (int i = 0; i < Grid::kRows; ++i)
    for (int j = 0; j < Grid::kCols; ++j) {
      const Vec2 p = g.previous_position(i, j), r = g.rest(i, j), uv = g.uv(i, j);
      out.push_back({p.x - r.x, p.y - r.y, t.depth, uv.x, uv.y, 0.0f, 0.0f});
    }
}

std::vector<uint16_t> grid_warp_indices() {
  std::vector<uint16_t> idx;
  for (int r = 0; r < Grid::kRows - 1; ++r)
    for (int c = 0; c < Grid::kCols - 1; ++c) {
      const int a = r * Grid::kCols + c, b = a + Grid::kCols;
      for (int v : {a, a + 1, b + 1, b}) idx.push_back(uint16_t(v));
    }
  return idx;
}

float grid_fade_step(float fade, bool in_game) {
  // fmadds(target - fade, 0.05 (dat_82000D60), fade).
  const float delta = in_game ? 1.0f - fade : -fade;
  return std::fmaf(delta, 0.05f, fade);
}

void emit_light(std::vector<LightVertex>& out, float size, float x, float y, const float colour[3]) {
  constexpr float kScale = 1.5f;  // dat_82000C90
  const float x0 = x - size, x1 = x + size, y0 = y - size, y1 = y + size;
  const float r = colour[0] * kScale, g = colour[1] * kScale, b = colour[2] * kScale;
  out.push_back({x0, y0, 0.0f, 0.0f, r, g, b});
  out.push_back({x1, y0, 1.0f, 0.0f, r, g, b});
  out.push_back({x1, y1, 1.0f, 1.0f, r, g, b});
  out.push_back({x0, y1, 0.0f, 1.0f, r, g, b});
}

std::vector<uint8_t> light_sprite_texture() {
  // Coordinates fmuls(fmsubs(k, 1/15, 1), 3.141) (dat_82001E84,
  // dat_820006C4, dat_82002918); sigma 1.0 (dat_82000C78, f64); value
  // fctiwz(float(density) * 255) clamped (dat_8200291C).
  constexpr float kStep = 0.0666666701f, kSpan = 3.14100003f;
  auto coord = [](int k) { return std::fmaf(float(k), kStep, -1.0f) * kSpan; };
  std::vector<uint8_t> tex(size_t(kLightSpriteSize) * kLightSpriteSize);
  for (int i = 0; i < kLightSpriteSize; ++i)
    for (int j = 0; j < kLightSpriteSize; ++j) {
      const float d = float(gauss(coord(j), coord(i), 1.0));
      const int v = int(d * 255.0f);
      tex[size_t(i) * kLightSpriteSize + size_t(j)] = uint8_t(v < 0 ? 0 : (v > 255 ? 255 : v));
    }
  return tex;
}

}  // namespace geow2

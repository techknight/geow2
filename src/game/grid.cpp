#include "game/grid.h"

#include <algorithm>
#include <cmath>

namespace geow2 {
namespace {

// Layout (static initialisers 0x8216CF50..0x8216D0D8, fixed scales).
constexpr float kMargin = 20.0f;           // dat_82002744
constexpr float kCellScale = 0.0666666701f;  // dat_821825A8 / dat_821825AC (1/15)
constexpr float kRowStep = 0.0113636367f;  // dat_82002750 (1/88)
constexpr float kColStep = 0.0075757578f;  // dat_82002754 (1/132)
constexpr size_t kMaxRecords = 0x1000;
constexpr size_t kMaxLights = 32;

// Simulation constants (loaded at 0x8205B908..0x8205B9D8).
constexpr float kNeighbour = 0.23f;        // dat_82002788
constexpr float kSpring = 0.25f;           // dat_82000720
constexpr float kDamping = 0.95f;          // dat_82001174
constexpr double kStepScale = 0.4000000059604645;  // dat_82002798 (f64)
constexpr double kStill = 0.0010000000474974513;   // dat_82002790 (f64)
constexpr double kPushDamping = 0.75;      // dat_82002780 (f64)
constexpr double kLightCap = 256.0;        // dat_82002770 (f64)
constexpr double kLightReach = 0.005;      // dat_82002778 (f64)
constexpr float kEpsilon = 9.99999975e-06f;  // dat_82002378 (f32)
constexpr double kPlayerPull = 12.0;       // dat_820027A0 (f64)
constexpr double kSpecialPush = 3.5;       // dat_82001210 (f64)

// The original uses frsqrte (a hardware estimate); exact here (visual only).
double rsqrt(double v) { return 1.0 / std::sqrt(v); }

// fsel(a, b, c) = a >= 0 ? b : c
double fsel(double a, double b, double c) { return a >= 0.0 ? b : c; }

struct Layout {
  float off_x, off_y;
  int width, height;
};

Layout layout(float half_x, float half_y) {
  // Uses the arena the static initialisers saw: the shipped arena is fixed.
  const float w = (half_x + kMargin) * 2.0f, h = (half_y + kMargin) * 2.0f;
  return {half_x + kMargin, half_y + kMargin, int(std::fmaf(w, 0.0666666701f, 0.5f)),
          int(std::fmaf(h, 0.0666666701f, 0.5f))};
}

}  // namespace

void Grid::reset(float half_x, float half_y) {
  // sub_8205B000.
  const size_t n = size_t(kRows) * kCols;
  rest_.assign(n, {});
  uv_.assign(n, {});
  vel_.assign(n, {});
  for (int i = 0; i < kRows; ++i) {
    const float fi = float(i);
    const float y = std::fmaf(fi * 2.0f, kRowStep, -1.0f) * half_y;
    const float v = std::fmaf(-fi, kRowStep, 1.0f);
    for (int j = 0; j < kCols; ++j) {
      const float fj = float(j);
      rest_[index(i, j)] = {std::fmaf(fj * 2.0f, kColStep, -1.0f) * half_x, y};
      uv_[index(i, j)] = {fj * kColStep, v};
    }
  }
  pos_[0] = rest_;
  pos_[1] = rest_;
  cur_ = 0;
}

bool Grid::add_push(const GridPush& p) {
  // sub_8205A2B0 with the cell lookups sub_82059FC0 / sub_8205A100.
  if (records_.size() >= kMaxRecords) return false;
  const Layout l = layout(300.0f, 200.0f);
  const int cx = int((p.x + l.off_x) * kCellScale), cy = int((p.y + l.off_y) * kCellScale);
  if (cx < 0 || cy < 0 || cx >= l.width || cy >= l.height) return true;
  auto clamp_low = [](int v, int hi) { return v < 0 ? 0 : (v > hi ? hi : v); };
  const int x0 = clamp_low(int(float(double(p.x + l.off_x) - p.radius) * kCellScale), l.width - 1);
  const int y0 = clamp_low(int(float(double(p.y + l.off_y) - p.radius) * kCellScale), l.height - 1);
  const int x1raw = int(float(double(p.x + l.off_x) + p.radius) * kCellScale) + 1;
  const int y1raw = int(float(double(p.y + l.off_y) + p.radius) * kCellScale) + 1;
  const int x1 = x1raw < 0 ? 0 : std::min(x1raw, l.width - 1);
  const int y1 = y1raw < 0 ? 0 : std::min(y1raw, l.height - 1);
  for (int y = y0; y < y1; ++y)
    for (int x = x0; x < x1; ++x) {
      if (records_.size() >= kMaxRecords) return false;
      records_.push_back({y * l.width + x, p.x, p.y, float(double(p.radius) * p.radius), p.strength});
    }
  return true;
}

void Grid::step(const GridInputs& in, float half_x, float half_y) {
  // Gather (sub_8205B578): records in gather order, stopping a group at the
  // record limit, then a stable counting sort by cell (sub_8205A3E0).
  records_.clear();
  for (const auto& group : in.pushes)
    for (const GridPush& p : group)
      if (!add_push(p)) break;
  const Layout l = layout(300.0f, 200.0f);
  const int cells = l.width * l.height + 1;
  cell_start_.assign(size_t(cells) + 1, 0);
  for (const Record& r : records_) ++cell_start_[size_t(r.cell) + 1];
  for (int c = 0; c < cells; ++c) cell_start_[size_t(c) + 1] += cell_start_[size_t(c)];
  std::vector<Record> sorted(records_.size());
  std::vector<int> fill(cell_start_.begin(), cell_start_.end() - 1);
  for (const Record& r : records_) sorted[size_t(fill[size_t(r.cell)]++)] = r;

  const size_t nlights = std::min(in.lights.size(), kMaxLights);
  const std::vector<Vec2>& prev = pos_[cur_];
  std::vector<Vec2>& next = pos_[1 - cur_];
  for (int i = 1; i <= 87; ++i) {
    for (int j = 1; j <= 131; ++j) {
      const int k = index(i, j);
      const float px = prev[size_t(k)].x, py = prev[size_t(k)].y;
      float ax = 0.0f, ay = 0.0f;
      const int cx = int((px + l.off_x) * kCellScale), cy = int((py + l.off_y) * kCellScale);

      for (size_t n = 0; n < nlights; ++n) {
        const GridLight& L = in.lights[n];
        const float dx = L.x - px, dy = L.y - py;
        const float d2 = (dy * dy + dx * dx) + kEpsilon;
        const double r = rsqrt(d2);
        double f = fsel(double(L.strength) - r * kLightCap, r * kLightCap, L.strength);
        f = f * L.scale * r;
        f = fsel(r - kLightReach, f, 0.0);
        const double nx = std::fma(double(dx), f, ax), ny = std::fma(double(dy), f, ay);
        ax = float(fsel(r - 1.0, ax, nx));
        ay = float(fsel(r - 1.0, ay, ny));
      }
      if (unsigned(cx) < unsigned(l.width) && unsigned(cy) < unsigned(l.height)) {
        const int cell = cy * l.width + cx;
        for (int n = cell_start_[size_t(cell)]; n < cell_start_[size_t(cell) + 1]; ++n) {
          const Record& R = sorted[size_t(n)];
          const double dax = double(ax) * kPushDamping, day = double(ay) * kPushDamping;
          const float dx = px - R.x, dy = py - R.y;
          const float d2 = dy * dy + dx * dx;
          const float outside = d2 - R.radius2;
          const double inv = rsqrt(fsel(double(d2) - 1.0, d2, 1.0)) * R.strength;
          const double nx = std::fma(double(dx), inv, dax), ny = std::fma(double(dy), inv, day);
          ax = float(fsel(outside, ax, nx));
          ay = float(fsel(outside, ay, ny));
        }
      }
      if (in.fading_player) {
        const float dx = in.fading_player->x - px, dy = in.fading_player->y - py;
        const double r = rsqrt((dy * dy + dx * dx) + kEpsilon);
        const double f = r * kPlayerPull;
        const double nx = std::fma(double(dx), f, ax), ny = std::fma(double(dy), f, ay);
        ax = float(fsel(r - 1.0, ax, nx));
        ay = float(fsel(r - 1.0, ay, ny));
      }
      if (in.special) {
        const float dx = in.special->x - px, dy = in.special->y - py;
        const double r = rsqrt((dy * dy + dx * dx) + kEpsilon);
        const double f = r * kSpecialPush;
        const double nx = -std::fma(double(dx), f, -double(ax)), ny = -std::fma(double(dy), f, -double(ay));
        ax = float(fsel(r - 1.0, ax, nx));
        ay = float(fsel(r - 1.0, ay, ny));
      }

      // Spring toward the rest position, coupled to the four neighbours'
      // displacements (previous frame), then damping and the position step.
      auto disp = [&](int n) {
        return Vec2{prev[size_t(n)].x - rest_[size_t(n)].x, prev[size_t(n)].y - rest_[size_t(n)].y};
      };
      const Vec2 up = disp(index(i - 1, j)), down = disp(index(i + 1, j)), left = disp(index(i, j - 1)),
                 right = disp(index(i, j + 1));
      const float sx = ((up.x + down.x) + left.x) + right.x;
      const float sy = ((up.y + down.y) + left.y) + right.y;
      const Vec2 rest = rest_[size_t(k)];
      float vx = std::fmaf((rest.x + sx * kNeighbour) - px, kSpring, vel_[size_t(k)].x);
      float vy = std::fmaf((rest.y + sy * kNeighbour) - py, kSpring, vel_[size_t(k)].y);
      vx = (vx + ax) * kDamping;
      vy = (vy + ay) * kDamping;
      vx = float(fsel(std::fabs(double(vx)) - kStill, vx, 0.0));
      vy = float(fsel(std::fabs(double(vy)) - kStill, vy, 0.0));
      vel_[size_t(k)] = {vx, vy};
      double nx = std::fma(double(vx), kStepScale, px), ny = std::fma(double(vy), kStepScale, py);
      nx = fsel(-double(half_x) - nx, -half_x, nx);
      nx = fsel(nx - double(half_x), half_x, nx);
      ny = fsel(-double(half_y) - ny, -half_y, ny);
      ny = fsel(ny - double(half_y), half_y, ny);
      next[size_t(k)] = {float(nx), float(ny)};
    }
  }
  cur_ = 1 - cur_;
}

}  // namespace geow2

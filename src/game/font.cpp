#include "game/font.h"
#include "game/fused.h"

#include <cmath>
#include <cstring>
#include <stdexcept>

namespace geow2 {
namespace {

constexpr uint32_t kBase = 0x82000000;
constexpr uint32_t kGlyphs = 0x821826A0;
constexpr int kGlyphCount = 123;
constexpr uint32_t kGlyphSize = 0x16C;

// Render.Ui.LineWidth (0x821A3054, value 0x821A3074).
constexpr float kUiLineWidth = 1.75f;

float be_float(std::span<const uint8_t> image, uint32_t address) {
  const size_t o = address - kBase;
  if (o + 4 > image.size()) throw std::runtime_error("font outside the image");
  const uint32_t bits =
      (uint32_t(image[o]) << 24) | (uint32_t(image[o + 1]) << 16) | (uint32_t(image[o + 2]) << 8) | image[o + 3];
  float f;
  std::memcpy(&f, &bits, 4);
  return f;
}

uint32_t be_u32(std::span<const uint8_t> image, uint32_t address) {
  const size_t o = address - kBase;
  if (o + 4 > image.size()) throw std::runtime_error("font outside the image");
  return (uint32_t(image[o]) << 24) | (uint32_t(image[o + 1]) << 16) | (uint32_t(image[o + 2]) << 8) | image[o + 3];
}

bool lower(int c) { return unsigned(c - 0x61) <= 0x19u; }

struct Kern {
  int a, b;
  float gap;
};
// The kerning pairs of sub_82075AD8 (dat_82003010, dat_82003014,
// dat_82000D54, dat_8200300C).
constexpr Kern kKerning[] = {
    {'W', 'A', 0.112500004f}, {'W', 0x07, 0.1875f}, {'A', 'W', 0.112500004f}, {'A', 'V', 0.112500004f},
    {'A', 'T', 0.075000003f}, {'A', 'Y', 0.262499988f}, {'L', 'V', 0.112500004f}, {'L', 'Y', 0.112500004f},
    {'L', 'T', 0.1875f},      {'V', 'A', 0.112500004f}, {'T', 'A', 0.075000003f}, {'F', 'A', 0.075000003f},
    {'Y', 'A', 0.262499988f},
};

// The quads, as the original computes them (in double, one rounding per
// stored value); the reciprocal square root is exact here (frsqrte in the
// original, DIFFERENCES.md).
struct UiLine {
  double X0, Y0, X1, Y1, A, B, zx, zy, Dx, ux, uy, hw;
};

UiLine ui_line(float x0, float y0, float x1, float y1, float factor) {
  // Render.Ui.TextX / TextY / TextZ (0x821A38B4, 0x821A40F4, 0x821A4934)
  // are 0.
  const float X0 = 0.0f + x0, X1 = 0.0f + x1, Y0 = 0.0f + y0, Y1 = 0.0f + y1;
  const float dx = X1 - X0, dy = Y1 - Y0;
  const double l = std::fma(double(dy), double(dy), double(dx) * double(dx)) + 9.999999747378752e-06;  // dat_82002760
  const double i = 1.0 / std::sqrt(l);
  UiLine u;
  u.X0 = X0;
  u.Y0 = Y0;
  u.X1 = X1;
  u.Y1 = Y1;
  // Half-width: (LineWidth * dat_821825F4) * 0.75 (dat_82000718).
  u.hw = (kUiLineWidth * factor) * 0.75f;
  u.ux = i * dx;
  u.uy = i * dy;
  u.A = u.hw * u.uy;
  u.B = u.hw * (u.ux * -1.0);  // dat_82000C80
  u.zx = u.ux * 0.0;           // dat_82000D30
  u.zy = u.uy * 0.0;
  u.Dx = u.hw * u.ux;
  return u;
}

LineVertex vertex(double x, double y, float side, const float rgb[3], float a) {
  return LineVertex{float(x), float(y), 0.0f, side, rgb[0], rgb[1], rgb[2], a};
}

}  // namespace

void emit_ui_line(LineBatch& out, float x0, float y0, float x1, float y1, const float colour[4], float factor) {
  const UiLine u = ui_line(x0, y0, x1, y1, factor);
  const float rgb[3] = {colour[0] * 8.0f, colour[1] * 8.0f, colour[2] * 8.0f};  // dat_82000D18
  const float a = colour[3];
  out.body.push_back(vertex((u.X0 - u.A) - u.zx, (u.Y0 - u.B) - u.zy, -1.0f, rgb, a));
  out.body.push_back(vertex((u.X0 + u.A) - u.zx, (u.Y0 + u.B) - u.zy, 1.0f, rgb, a));
  out.body.push_back(vertex((u.X1 + u.zx) + u.A, (u.Y1 + u.zy) + u.B, 1.0f, rgb, a));
  out.body.push_back(vertex((u.X1 - u.A) + u.zx, (u.Y1 - u.B) + u.zy, -1.0f, rgb, a));
  out.caps.push_back(vertex(u.X0 - u.A, u.Y0 - u.B, -1.0f, rgb, 1.0f));
  out.caps.push_back(vertex(u.X0 + u.A, u.Y0 + u.B, 1.0f, rgb, 1.0f));
  out.caps.push_back(vertex((u.X0 + u.A) - u.Dx, (u.Y0 + u.B) - u.A, 1.0f, rgb, 0.0f));
  out.caps.push_back(vertex((u.X0 - u.A) - u.Dx, (u.Y0 - u.B) - u.A, -1.0f, rgb, 0.0f));
  out.caps.push_back(vertex(u.X1 - u.A, u.Y1 - u.B, -1.0f, rgb, 1.0f));
  out.caps.push_back(vertex(u.X1 + u.A, u.Y1 + u.B, 1.0f, rgb, 1.0f));
  out.caps.push_back(vertex(std::fma(u.uy + u.ux, u.hw, u.X1), std::fma(-u.ux + u.uy, u.hw, u.Y1), 1.0f, rgb, 0.0f));
  out.caps.push_back(vertex(std::fma(u.hw, u.ux, u.X1 - u.A), (u.Y1 - u.B) + u.A, -1.0f, rgb, 0.0f));
}

void emit_ui_body(LineBatch& out, float x0, float y0, float x1, float y1, const float colour0[4],
                  const float colour1[4], float factor) {
  const UiLine u = ui_line(x0, y0, x1, y1, factor);
  const float c0[3] = {colour0[0] * 8.0f, colour0[1] * 8.0f, colour0[2] * 8.0f};
  const float c1[3] = {colour1[0] * 8.0f, colour1[1] * 8.0f, colour1[2] * 8.0f};
  out.body.push_back(vertex((u.X0 - u.A) - u.zx, (u.Y0 - u.B) - u.zy, -1.0f, c0, colour0[3]));
  out.body.push_back(vertex((u.X0 + u.A) - u.zx, (u.Y0 + u.B) - u.zy, 1.0f, c0, colour0[3]));
  out.body.push_back(vertex((u.X1 + u.zx) + u.A, (u.Y1 + u.zy) + u.B, 1.0f, c1, colour1[3]));
  out.body.push_back(vertex((u.X1 - u.A) + u.zx, (u.Y1 - u.B) + u.zy, -1.0f, c1, colour1[3]));
}

void emit_ui_caps(LineBatch& out, float x0, float y0, float x1, float y1, const float colour0[4],
                  const float colour1[4], float factor) {
  const UiLine u = ui_line(x0, y0, x1, y1, factor);
  const float c0[3] = {colour0[0] * 8.0f, colour0[1] * 8.0f, colour0[2] * 8.0f};
  const float c1[3] = {colour1[0] * 8.0f, colour1[1] * 8.0f, colour1[2] * 8.0f};
  out.caps.push_back(vertex(u.X0 - u.A, u.Y0 - u.B, -1.0f, c0, 1.0f));
  out.caps.push_back(vertex(u.X0 + u.A, u.Y0 + u.B, 1.0f, c0, 1.0f));
  out.caps.push_back(vertex((u.X0 + u.A) - u.Dx, (u.Y0 + u.B) - u.A, 1.0f, c0, 0.0f));
  out.caps.push_back(vertex((u.X0 - u.A) - u.Dx, (u.Y0 - u.B) - u.A, -1.0f, c0, 0.0f));
  out.caps.push_back(vertex(u.X1 - u.A, u.Y1 - u.B, -1.0f, c1, 1.0f));
  out.caps.push_back(vertex(u.X1 + u.A, u.Y1 + u.B, 1.0f, c1, 1.0f));
  out.caps.push_back(vertex(std::fma(u.uy + u.ux, u.hw, u.X1), std::fma(-u.ux + u.uy, u.hw, u.Y1), 1.0f, c1, 0.0f));
  out.caps.push_back(vertex(std::fma(u.hw, u.ux, u.X1 - u.A), (u.Y1 - u.B) + u.A, -1.0f, c1, 0.0f));
}

void emit_ui_line2(LineBatch& out, float x0, float y0, float x1, float y1, const float colour0[4],
                   const float colour1[4], float factor) {
  emit_ui_body(out, x0, y0, x1, y1, colour0, colour1, factor);
  emit_ui_caps(out, x0, y0, x1, y1, colour0, colour1, factor);
}

Font Font::from_image(std::span<const uint8_t> image) {
  Font f;
  f.full_size_.fill(1);
  f.map_.fill(0);
  for (int i = 0; i < kGlyphCount; ++i) {
    const uint32_t g = kGlyphs + uint32_t(i) * kGlyphSize;
    Glyph glyph;
    glyph.code = int(be_u32(image, g) & 0xFF);
    glyph.width = be_float(image, g + 4);
    for (int k = 0; k < 22; ++k) {
      const uint32_t s = g + 8 + uint32_t(k) * 16;
      const float x0 = be_float(image, s);
      if (!(x0 >= -50.0f)) break;  // dat_82003008
      glyph.segments.push_back({x0, be_float(image, s + 4), be_float(image, s + 8), be_float(image, s + 12)});
    }
    const int c = glyph.code;
    f.map_[size_t(c)] = uint8_t(i);
    if (c >= 0x41 && c <= 0x5A) {
      f.map_[size_t(c + 0x20)] = uint8_t(i);
      f.full_size_[size_t(c + 0x20)] = 0;
    }
    f.glyphs_.push_back(std::move(glyph));
  }
  auto alias = [&](int from, int to, int count) {
    for (int k = 0; k < count; ++k) {
      f.map_[size_t(to + k)] = f.map_[size_t(from + k)];
      f.full_size_[size_t(to + k)] = 0;
    }
  };
  alias(0xC0, 0xE0, 16);
  alias(0xD1, 0xF1, 6);
  alias(0xD8, 0xF8, 5);
  alias(0x8C, 0x9C, 1);
  return f;
}

float Font::advance(int c, int next, int& state) const {
  if (c == 4) {
    state ^= 1;
    return 0.0f;
  }
  if (c == 7) {
    state ^= 2;
    return 0.0f;
  }
  float s = 1.0f;
  if (lower(c)) {
    c -= 0x20;
    s = 0.75f;  // dat_82000718
  } else if (!full_size_[size_t(c & 0xFF)]) {
    s = 0.75f;
  }
  if (lower(next)) next -= 0x20;
  float gap = 0.375f;  // dat_820022FC
  bool paired = false;
  for (const Kern& k : kKerning)
    if (kerning_ && k.a == c && k.b == next) {
      gap = k.gap;
      paired = true;
      break;
    }
  if (!paired && next == 0) gap = 0.0f;
  return fmadds(glyphs_[map_[size_t(c & 0xFF)]].width, s, gap);
}

float Font::measure(const std::string& s) const {
  int state = 0;
  float sum = 0.0f;
  for (size_t i = 0; i < s.size(); ++i) {
    const int c = int(int8_t(s[i]));
    const int next = i + 1 < s.size() ? int(int8_t(s[i + 1])) : 0;
    sum = advance(c, next, state) + sum;
  }
  return sum;
}

float Font::glyph(LineBatch& out, float x, float y, float w, float h, int c, const float colour[4], int next,
                  int& state) const {
  return glyph_at(out, x, y, w, h, c, colour, next, state, false);
}

void Font::world_left(LineBatch& out, float x, float y, float w, float h, const float colour[4],
                      const std::string& s) const {
  int state = 0;
  for (size_t i = 0; i < s.size(); ++i) {
    const int c = int(int8_t(s[i]));
    const int next = i + 1 < s.size() ? int(int8_t(s[i + 1])) : 0;
    x = glyph_at(out, x, y, w, h, c, colour, next, state, true) + x;
  }
}

float Font::glyph_at(LineBatch& out, float x, float y, float w, float h, int c, const float colour[4], int next,
                     int& state, bool world) const {
  const Glyph& g = glyphs_[map_[size_t(c & 0xFF)]];
  float W = g.width * w;
  const float adv = advance(c, next, state) * w;
  if (c == 4 || c == 7) return adv;
  float H = h, Y = y;
  if (state & 1) Y = fmadds(-h, 0.25f, Y);  // dat_82000720
  if (state & 2) {
    H = h * 0.5f;  // dat_82000724
    Y = H + Y;
  }
  if (lower(c) || !full_size_[size_t(c & 0xFF)]) {
    Y = fmadds(H, 0.25f, Y);
    H = H * 0.75f;
    W = W * 0.75f;
  }
  // Codes 3 and 5: the segments past the fourth / fifth have no caps.
  const size_t capped = c == 3 ? 4 : c == 5 ? 5 : g.segments.size();
  for (size_t k = 0; k < g.segments.size(); ++k) {
    const auto& s = g.segments[k];
    const float x0 = fmadds(s[0], W, x), y0 = fmadds(s[1], H, Y);
    const float x1 = fmadds(s[2], W, x), y1 = fmadds(s[3], H, Y);
    if (world)
      emit_line(out, kLineWidthUnit, 0.3f, {x0, y0, 0.0f, 1.0f}, {x1, y1, 0.0f, 1.0f}, colour);  // dat_82000708
    else if (k < capped)
      emit_ui_line(out, x0, y0, x1, y1, colour, line_factor_);
    else
      emit_ui_body(out, x0, y0, x1, y1, colour, colour, line_factor_);
  }
  return adv;
}

void Font::run(LineBatch& out, float x, float y, float w, float h, const float colour[4], const std::string& s) const {
  int state = 0;
  for (size_t i = 0; i < s.size(); ++i) {
    const int c = int(int8_t(s[i]));
    const int next = i + 1 < s.size() ? int(int8_t(s[i + 1])) : 0;
    x = glyph(out, x, y, w, h, c, colour, next, state) + x;
  }
}

void Font::left(LineBatch& out, float x, float y, float w, float h, const float colour[4], const std::string& s) const {
  run(out, x, y, w, h, colour, s);
}

void Font::right(LineBatch& out, float x, float y, float w, float h, const float colour[4], const std::string& s) const {
  run(out, fmadds(-measure(s), w, x), y, w, h, colour, s);
}

void Font::centre(LineBatch& out, float x, float y, float w, float h, const float colour[4],
                  const std::string& s) const {
  run(out, fmadds(-(measure(s) * w), 0.5f, x), y, w, h, colour, s);
}

void Font::right_fit(LineBatch& out, float x, float y, float w, float h, float max_w, const float colour[4],
                     const std::string& s) const {
  const float m = measure(s);
  if (m * w > max_w) w = (w / (m * w)) * max_w;
  run(out, fmadds(-m, w, x), y, w, h, colour, s);
}

void Font::centre_fit(LineBatch& out, float x, float y, float w, float h, float max_w, const float colour[4],
                      const std::string& s) const {
  const float m = measure(s);
  if (m * w > max_w) w = (w / (m * w)) * max_w;
  run(out, fmadds(-(m * w), 0.5f, x), y, w, h, colour, s);
}

void Font::left_fit(LineBatch& out, float x, float y, float w, float h, float max_w, const float colour[4],
                    const std::string& s) const {
  const float m = measure(s);
  if (m * w > max_w) w = (w / (m * w)) * max_w;
  run(out, x, y, w, h, colour, s);
}

namespace {
int g_language = 1;  // dat_8218E778
}  // namespace

void set_language(int language) { g_language = language; }
int language() { return g_language; }

std::string format_number(int64_t v) {
  // "%I64i", then a separator after every third digit from the right.
  const char sep = g_language == 3 || g_language == 5 || g_language == 6 ? '.' : g_language == 4 ? '\x01' : ',';
  const std::string digits = std::to_string(v);
  std::string out;
  int n = 0;
  for (size_t i = digits.size(); i-- > 0;) {
    out.insert(out.begin(), digits[i]);
    if (++n % 3 == 0 && i > 0) out.insert(out.begin(), sep);
  }
  return out;
}

}  // namespace geow2

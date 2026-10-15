#include "game/ui.h"

#include <cmath>
#include <cstdio>

namespace geow2 {
namespace {

// The Ui.Colour.* defaults (static initialisers 0x82174A38..0x821763E8;
// each value's constant cited in colourvars.txt): rgb, alpha.
constexpr float kColours[kUiSlotCount][4] = {
    {0.0879999995f, 0.0f, 1.00300002f, 1.0f},         // Background
    {1.0f, 0.219999999f, 0.0f, 1.0f},                 // Selection
    {0.143000007f, 1.21899998f, 0.0f, 1.0f},          // Normal
    {0.272000015f, 1.20000005f, 0.050999999f, 2.0f},  // Bright
    {0.638999999f, 1.10000002f, 0.344000012f, 1.0f},  // Highlight
    {0.0149999997f, 0.0199999996f, 0.100000001f, 1.0f},  // Disabled
    {0.100000001f, 0.0219999999f, 0.0f, 1.0f},        // SelectionDisabled
    {1.0f, 0.219999999f, 0.0f, 1.0f},                 // ScrollBar
    {0.0152000003f, 0.0104f, 0.071800001f, 1.0f},     // LockedBackground
    {0.0152000003f, 0.0104f, 0.071800001f, 1.0f},     // LockedNormal
    {0.216000006f, 0.216000006f, 0.216000006f, 1.0f},  // LockedBackgroundPending
    {0.216000006f, 0.216000006f, 0.216000006f, 1.0f},  // LockedNormalPending
    {1.10000002f, 0.189999998f, 0.189999998f, 1.0f},  // MultiplayerRed
    {0.189999998f, 0.189999998f, 1.20000005f, 1.0f},  // MultiplayerBlue
    {1.0f, 0.5f, 0.129999995f, 1.0f},                 // MultiplayerYellow
    {0.189999998f, 1.0f, 0.189999998f, 1.0f},         // MultiplayerGreen
    {0.119000003f, 0.0670000017f, 0.246999994f, 1.0f},  // MultiplayerDisabled
    {0.5f, 1.0f, 0.200000003f, 1.25f},                // IntermissionSelection
    {0.0879999995f, 0.0f, 1.0f, 0.649999976f},        // IntermissionUnplayed
    {0.143000007f, 1.21899998f, 0.0f, 1.0f},          // IntermissionSuccess
    {1.0f, 0.219999999f, 0.0f, 0.75f},                // IntermissionDied
    {1.0f, 0.219999999f, 0.0f, 0.75f},                // IntermissionOutOfTime
    {0.143000007f, 1.21899998f, 0.0f, 1.0f},          // GraphAxis
    {1.0f, 0.219999999f, 0.0f, 1.0f},                 // GraphScore
    {1.0f, 0.899999976f, 0.0f, 1.0f},                 // GraphMultiplier
    {0.638999999f, 1.10000002f, 0.344000012f, 1.0f},  // GraphLabel
};

// sub_82076A80: the advance of a run of characters, '[' and ']' skipped.
float run_advance(const Font& font, const std::string& s) {
  int state = 0;
  float sum = 0.0f;
  for (size_t i = 0; i < s.size(); ++i) {
    const int c = int(int8_t(s[i]));
    if (c == '[' || c == ']') continue;
    const int next = i + 1 < s.size() ? int(int8_t(s[i + 1])) : 0;
    sum = font.advance(c, next, state) + sum;
  }
  return sum;
}

}  // namespace

UiPalette UiPalette::defaults() {
  UiPalette p;
  for (int i = 0; i < kUiSlotCount; ++i) {
    const float a = kColours[i][3];
    p.slot[size_t(i)] = {kColours[i][0] * a, kColours[i][1] * a, kColours[i][2] * a, 1.0f};
  }
  return p;
}

Rgba scaled(const Rgba& c, float k) { return {c[0] * k, c[1] * k, c[2] * k, c[3] * k}; }
Rgba scaled_rgb(const Rgba& c, float k) { return {c[0] * k, c[1] * k, c[2] * k, c[3]}; }

void ui_panel(UiCanvas& cv, float x0, float y0, float x1, float y1, const Rgba& colour, uint32_t flags) {
  const Rgba C = scaled(colour, cv.fade);
  const float wd = x1 - x0, ht = y1 - y0;
  const int nx = int(wd) / 10 + 1, ny = int(ht) / 10 + 1;
  const float grid[4] = {C[0] * 0.01875f, C[1] * 0.01875f, C[2] * 0.01875f, C[3] * 0.15f};  // dat_82006268, dat_82000D58
  // Brightest at the screen's centre: 5 - |x - 320| * 0.01484375
  // (dat_820006F0, dat_82006264).
  auto at = [&](float x) {
    const float k = std::fmaf(-std::fabs(x - 320.0f), 0.01484375f, 5.0f);
    return Rgba{C[0] * k, C[1] * k, C[2] * k, C[3] * k};
  };
  const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  for (int i = 0; i < nx; ++i) {
    const float xa = std::fmaf(float(i) / float(nx), wd, x0);
    const float xb = std::fmaf(float(i + 1) / float(nx), wd, x0);
    const Rgba ca = at(xa), cb = at(xb);
    if (!(flags & 8)) emit_ui_body(cv.lines, xa, y0, xb, y0, ca.data(), cb.data());
    if (!(flags & 0x10)) emit_ui_body(cv.lines, xa, y1, xb, y1, ca.data(), cb.data());
    if (flags & 1) continue;
    // The opaque fill (sub_82091E98).
    cv.fill.push_back({xa, y0, black[0], black[1], black[2], black[3]});
    cv.fill.push_back({xb, y0, black[0], black[1], black[2], black[3]});
    cv.fill.push_back({xb, y1, black[0], black[1], black[2], black[3]});
    cv.fill.push_back({xa, y1, black[0], black[1], black[2], black[3]});
    if (flags & 0x20) continue;
    emit_ui_line(cv.lines, xa, y0, xa, y1, grid);
    for (int j = 1; j < ny; ++j) {
      const float yj = std::fmaf(float(j) / float(ny), ht, y0);
      emit_ui_line(cv.lines, xa, yj, xb, yj, grid);
    }
  }
  const Rgba c0 = at(x0), c1 = at(x1);
  if (!(flags & 8)) emit_ui_caps(cv.lines, x0, y0, x1, y0, c0.data(), c1.data());
  if (!(flags & 0x10)) emit_ui_caps(cv.lines, x0, y1, x1, y1, c0.data(), c1.data());
  if (!(flags & 2)) emit_ui_line2(cv.lines, x0, y0, x0, y1, c0.data(), c0.data());
  if (!(flags & 4)) emit_ui_line2(cv.lines, x1, y0, x1, y1, c1.data(), c1.data());
}

void UiCanvas::sprite(float x, float y, float w, float h, const Rgba& colour, int texture, bool flat) {
  if (sprites.size() >= 64) return;
  UiSprite s;
  s.x = x;
  s.y = y;
  s.w = w;
  s.h = h;
  s.texture = texture;
  s.r = colour[0] * 8.0f;  // dat_82000D18
  s.g = colour[1] * 8.0f;
  s.b = colour[2] * 8.0f;
  s.a = colour[3];
  s.flat = flat;
  sprites.push_back(s);
}

void ui_bar(UiCanvas& c, const UiPalette& p, float y0, float y1) { ui_panel(c, 0.0f, y0, 640.0f, y1, p[kUiBackground], 6); }

TextLayout::Field* TextLayout::add_empty() {
  if (fields_.size() >= 24) return nullptr;
  // The original's 24 fields are a fixed array: reserved, so the fields
  // handed out stay put.
  fields_.reserve(24);
  fields_.emplace_back();
  return &fields_.back();
}

TextLayout::Field* TextLayout::add(const Rgba& colour, const std::string& text) {
  Field* f = add_empty();
  if (!f) return nullptr;
  f->colour = colour;
  f->has_text = true;
  // The pool holds 0x200 bytes; text past it is cut, a full pool gives "".
  if (used_ >= 0x200) {
    f->text.clear();
  } else {
    const size_t room = 0x200 - used_ - 1;
    f->text = text.substr(0, room);
    used_ = text.size() > room ? 0x200 : used_ + f->text.size() + 1;
  }
  return f;
}

void TextLayout::add_split(const Rgba& a, const Rgba& b, const std::string& text) {
  size_t start = 0;
  bool first = true;
  for (size_t i = 0; i <= text.size(); ++i) {
    if (i < text.size() && text[i] != '~') continue;
    if (!add(first ? a : b, text.substr(start, i - start))) return;
    first = !first;
    start = i + 1;
  }
}

void TextLayout::set_colour(const Rgba& c) {
  for (Field& f : fields_) f.colour = c;
}

void TextLayout::transform(const Rgba& add, const Rgba& mul) {
  for (Field& f : fields_)
    for (int i = 0; i < 4; ++i) f.colour[size_t(i)] = add[size_t(i)] + mul[size_t(i)] * f.colour[size_t(i)];
}

float TextLayout::width(const Font& font, float w) const {
  float total = 0.0f;
  for (const Field& f : fields_) {
    float fw = f.has_text ? (font.measure(f.text) * f.scale) * w : 0.0f;
    fw = fw - f.min_width >= 0.0f ? fw : f.min_width;
    total = fw + f.pad_before + total;
    total = f.pad_after + total;
  }
  return total;
}

void TextLayout::draw(const Font& font, LineBatch& out, float x, float y, float w, float h, float max_w,
                      int align) const {
  float t = width(font, w);
  if (max_w >= 0.0f && max_w < t) {
    w = (w / t) * max_w;
    t = max_w;
  }
  bool fill = false;
  for (const Field& f : fields_)
    if (f.align == 3) fill = true;
  const float k = align == 1 ? 1.0f : align == 2 ? 0.5f : 0.0f;
  if (align != 0) x = x - k * (fill && max_w > 0.0f ? max_w : t);
  float c = 0.0f;
  for (const Field& f : fields_) {
    const float c1 = f.pad_before + c;
    if (f.has_text) {
      const float fr = (font.measure(f.text) * f.scale) * w;
      const float fw = fr - f.min_width >= 0.0f ? fr : f.min_width;
      const float offset = f.align == 1 ? fw - fr : f.align == 2 ? (fw + fr) * 0.5f : 0.0f;
      font.left(out, x + (offset + c1), y, f.scale * w, h, f.colour.data(), f.text);
      c = fw + c1;
    } else {
      c = (f.align == 3 && max_w >= 0.0f) ? max_w - (t - c1) : f.min_width + c1;
    }
    c = f.pad_after + c;
  }
}

std::vector<std::string> wrap_text(const Font& font, const std::string& text, float w, float max_w, int max_lines) {
  std::vector<std::string> lines;
  size_t p = 0;
  const size_t n = text.size();
  while (p < n && text[p] == ' ') ++p;
  while (p < n) {
    if (int(lines.size()) + 1 >= max_lines) {
      // The rest becomes the last line.
      lines.push_back(text.substr(p));
      break;
    }
    // Characters while the sum stays under maxW; the one crossing it is
    // included.
    size_t q = p;
    float sum = 0.0f;
    int state = 0;
    while (q < n && sum < max_w) {
      const int c = int(int8_t(text[q]));
      const int next = q + 1 < n ? int(int8_t(text[q + 1])) : 0;
      sum = std::fmaf(font.advance(c, next, state), w, sum);
      ++q;
    }
    size_t stop = q;
    if (q < n && q != p) {
      // Back to the last space after the start.
      size_t s = q;
      while (s > p && text[s] != ' ') --s;
      if (s > p) stop = s;
    }
    size_t e = stop;
    while (e > p && text[e - 1] == ' ') --e;
    lines.push_back(text.substr(p, e - p));
    p = stop;
    while (p < n && text[p] == ' ') ++p;
  }
  return lines;
}

float text_box_height(const Font& font, const std::string& text, float w, float h, float gap, float max_w) {
  const int n = int(wrap_text(font, text, w, max_w).size());
  return std::fmaf(float(n - 1 > 0 ? n - 1 : 0), gap, float(n) * h);
}

float text_box(const Font& font, LineBatch& out, const UiPalette& p, float x, float y, float w, float h, float gap,
               float max_w, int h_align, int v_align, const Rgba& colour, const std::string& text) {
  const std::vector<std::string> lines = wrap_text(font, text, w, max_w);
  const int n = int(lines.size());
  const float height = std::fmaf(float(n - 1 > 0 ? n - 1 : 0), gap, float(n) * h);
  if (v_align == 1) y = std::fmaf(-height, 0.5f, y);
  if (v_align == 2) y = y - height;
  for (int i = 0; i < n; ++i) {
    const std::string& line = lines[size_t(i)];
    const float lw = run_advance(font, line) * w;
    float x0 = x;
    if (h_align == 1) x0 = x - lw;
    if (h_align == 2) x0 = std::fmaf(-lw, 0.5f, x);
    float extra = 0.0f;
    if (h_align == 4 && i + 1 < n) {
      int spaces = 0;
      for (char c : line) spaces += c == ' ';
      if (spaces > 0) extra = (max_w - lw) / float(spaces);
    }
    const Rgba* c = &colour;
    int state = 0;
    float cx = x0;
    for (size_t k = 0; k < line.size(); ++k) {
      const int ch = int(int8_t(line[k]));
      if (ch == '[') {
        c = &p[kUiHighlight];
        continue;
      }
      if (ch == ']') {
        c = &colour;
        continue;
      }
      const int next = k + 1 < line.size() ? int(int8_t(line[k + 1])) : 0;
      cx = font.glyph(out, cx, y, w, h, ch, c->data(), next, state) + cx;
      if (ch == ' ') cx = extra + cx;
    }
    y = (h + gap) + y;
  }
  return height;
}

std::string format(const char* fmt, ...) {
  char buf[0x300];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  return buf;
}

}  // namespace geow2

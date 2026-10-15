#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "game/line_quads.h"

namespace geow2 {

// The UI line emitter sub_8205FBA8: a line in UI units (640 x 360) as a
// body quad and two square end caps, half-width Render.Ui.LineWidth (1.75)
// times 0.75, colour rgb times 8 with its alpha.
// `factor` is dat_821825F4 (1, or 0.667 while the linger layer draws).
void emit_ui_line(LineBatch& out, float x0, float y0, float x1, float y1, const float colour[4], float factor = 1.0f);
// sub_820603C0: the body quad only, a colour per end.
void emit_ui_body(LineBatch& out, float x0, float y0, float x1, float y1, const float colour0[4],
                  const float colour1[4], float factor = 1.0f);

// sub_82060600: the two end caps only, colour0 at (x0, y0), colour1 at
// (x1, y1).
void emit_ui_caps(LineBatch& out, float x0, float y0, float x1, float y1, const float colour0[4],
                  const float colour1[4], float factor = 1.0f);
// sub_8205FFB8: body and caps, colour0 at the first end, colour1 at the
// second.
void emit_ui_line2(LineBatch& out, float x0, float y0, float x1, float y1, const float colour0[4],
                   const float colour1[4], float factor = 1.0f);

// The vector font (123 glyphs at 0x821826A0 in the user's image, 0x16C
// bytes each: code, width, up to 22 segments in a 1 x 1 box), with the
// map sub_82075818 builds: lower-case letters (and the accented ones) use
// the upper-case glyphs as small capitals.
class Font {
 public:
  static Font from_image(std::span<const uint8_t> image);
  // dat_821825F4: the line width factor text is drawn with.
  void set_line_factor(float f) const { line_factor_ = f; }
  // dat_821D1E30: set, the kerning pairs are skipped (every pair gets the
  // default gap).
  void set_kerning(bool on) const { kerning_ = on; }

  // sub_82075AD8: the advance after character c (followed by `next`), in
  // units of the width scale; codes 4 and 7 toggle raised and half-height
  // text in `state`.
  float advance(int c, int next, int& state) const;
  // sub_82075CD8.
  float measure(const std::string& s) const;
  // The text entry points: left (sub_82076130), right (sub_820762F0),
  // centred (sub_820764C0); the fitted ones shrink the width scale to
  // `max_w` (sub_820763D0 right, sub_820765A8 centred).
  void left(LineBatch& out, float x, float y, float w, float h, const float colour[4], const std::string& s) const;
  void right(LineBatch& out, float x, float y, float w, float h, const float colour[4], const std::string& s) const;
  void centre(LineBatch& out, float x, float y, float w, float h, const float colour[4], const std::string& s) const;
  void right_fit(LineBatch& out, float x, float y, float w, float h, float max_w, const float colour[4],
                 const std::string& s) const;
  void centre_fit(LineBatch& out, float x, float y, float w, float h, float max_w, const float colour[4],
                  const std::string& s) const;
  // sub_82076200: left-aligned, fitted.
  void left_fit(LineBatch& out, float x, float y, float w, float h, float max_w, const float colour[4],
                const std::string& s) const;
  // sub_82075DC8: one glyph; returns its advance.
  float glyph(LineBatch& out, float x, float y, float w, float h, int c, const float colour[4], int next,
              int& state) const;

 private:
  struct Glyph {
    int code = 0;
    float width = 0.0f;
    std::vector<std::array<float, 4>> segments;
  };
  void run(LineBatch& out, float x, float y, float w, float h, const float colour[4], const std::string& s) const;

  std::vector<Glyph> glyphs_;
  std::array<uint8_t, 256> map_{};
  std::array<uint8_t, 256> full_size_{};  // 0: drawn as a small capital
  mutable float line_factor_ = 1.0f;
  mutable bool kerning_ = true;
};

// sub_8205D740: a number with thousands separators (',' in English).
std::string format_number(int64_t v);

}  // namespace geow2

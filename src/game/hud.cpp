#include "game/hud.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace geow2 {
namespace {

// The UI palette (sub_82090DA8): a colour variable's rgb times its alpha,
// alpha 1. Normal: Ui.Colour.Normal (0x821F3ECC; dat_82006374,
// dat_8200638C); the warning colour: Ui.Colour.Selection (0x821F3E88;
// dat_82006370) times 2 (dat_820006FC).
constexpr float kNormal[4] = {0.143000007f * 1.0f, 1.21899998f * 1.0f, 0.0f, 1.0f};
constexpr float kWarning[4] = {1.0f * 2.0f, 0.219999999f * 2.0f, 0.0f * 2.0f, 1.0f * 2.0f};

std::string printf_string(const char* fmt, ...) {
  char buf[0x100];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  return buf;
}

// sub_8207C530(x, y, t, s, align, n, ch, force): a row of icons, pulsing
// brighter and bigger with t; above 3, the icon and a count.
void icon_row(const Font& font, LineBatch& out, float x, float y, float t, float s, int align, int n, char ch,
              bool force) {
  const float k = std::fmaf(t, 5.0f, 1.0f);
  const float c[4] = {kNormal[0] * k, kNormal[1] * k, kNormal[2] * k, kNormal[3]};
  const float size = std::fmaf(t, 4.0f, 12.0f) * s;
  const float yy = y - (t * s) * 2.0f;
  std::string text;
  if (n > 3 || force)
    text = align == 1 ? printf_string("%i%c", n, ch) : printf_string("%c%i", ch, n);
  else
    text = std::string(size_t(n < 0 ? 0 : n), ch);
  if (align == 0)
    font.left(out, x, yy, size, size, c, text);
  else if (align == 1)
    font.right(out, x, yy, size, size, c, text);
  else
    font.centre(out, x, yy, size, size, c, text);
}

}  // namespace

void draw_hud(const Font& font, const HudText& text, const HudState& s, LineBatch& out) {
  if (s.mode == 2) {
    // The level's time left, clamped to [0, D25C] (fsel), as seconds and
    // hundredths; red under ten seconds.
    const float d = s.level_time - s.level_timer;
    float t = d >= 0.0f ? d : 0.0f;
    t = d - s.level_time >= 0.0f ? s.level_time : t;
    const int secs = int(t), hundredths = int(t * 100.0f) % 100;
    font.centre_fit(out, 320.0f, 22.0f, 12.0f, 18.0f, 275.0f, secs < 10 ? kWarning : kNormal,
                    printf_string("%02i'%02i''", secs, hundredths));
    icon_row(font, out, 220.0f, 22.0f, s.lives_pulse, 0.625f, 2, s.lives, 0x0C, false);
    icon_row(font, out, 420.0f, 22.0f, s.bombs_pulse, 0.625f, 2, s.bombs, 0x02, false);
    font.left(out, 36.0f, 50.0f, 7.5f, 7.5f, kNormal, "x" + format_number(s.multiplier));  // dat_82001040
  } else if (s.lives_mode == 2) {
    // The time left, m:ss, red under ten seconds (fitted to 275).
    const int length = int(s.length);
    int left = length - int(s.clock);
    if (left < 0) left = 0;
    if (left > length) left = length;
    font.centre_fit(out, 320.0f, 22.0f, 12.0f, 18.0f, 275.0f, left < 10 ? kWarning : kNormal,
                    printf_string("%01i:%02i", left / 60, left % 60));
    font.centre(out, 220.0f, 22.0f, 7.5f, 7.5f, kNormal, "x" + format_number(s.multiplier));
    icon_row(font, out, 420.0f, 22.0f, s.bombs_pulse, 0.625f, 2, s.bombs, 0x02, false);
  } else if (s.start_lives == 0) {
    font.centre(out, 320.0f, 22.0f, 9.0f, 9.0f, kNormal, "x" + format_number(s.multiplier));
  } else {
    // Lives (char 0x0C) right-aligned left of the centre, bombs (0x02)
    // left-aligned right of it (dat_820049A8, dat_820049A4, size 1.0).
    icon_row(font, out, 317.0f, 22.0f, s.lives_pulse, 1.0f, 1, s.lives, 0x0C, false);
    icon_row(font, out, 323.0f, 22.0f, s.bombs_pulse, 1.0f, 0, s.bombs, 0x02, false);
    font.centre(out, 320.0f, 38.0f, 9.0f, 9.0f, kNormal, "x" + format_number(s.multiplier));  // dat_820049A0
  }
  // Score and high score (the label is the format string).
  font.left(out, 36.0f, 22.0f, 11.0f, 7.5f, kNormal, text.score);
  font.left(out, 36.0f, 34.0f, 11.0f, 12.0f, kNormal, format_number(s.score));
  const int64_t high = s.score > s.high_score ? s.score : s.high_score;
  font.right_fit(out, 604.0f, 22.0f, 11.0f, 7.5f, 160.0f, kNormal, text.high_score);
  font.right(out, 604.0f, 34.0f, 11.0f, 12.0f, kNormal, format_number(high));
}

}  // namespace geow2

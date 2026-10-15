#pragma once

#include <cstdint>
#include <string>

#include "game/font.h"

namespace geow2 {

// What the single-player HUD shows (the player record 0x8219D068 and the
// game clock).
struct HudState {
  int64_t score = 0;            // +0x28
  int64_t high_score = 0;       // the mode's best (local table)
  int multiplier = 1;           // +0x40
  int bombs = 0;                // +0x34
  int lives = 0;                // +0x30
  float lives_pulse = 0.0f;     // +0x58 (dat_8219D0C0)
  float bombs_pulse = 0.0f;     // +0x5C (dat_8219D0C4)
  int start_lives = 0;          // dat_8219D264: 0 hides the lives and bombs
  float clock = 0.0f;           // dat_8219CE68
  float length = 180.0f;        // dat_8219D258
  int lives_mode = 2;           // dat_8219D24C (2: the m:ss timer)
};

// The HUD's strings: ID_X_SCORE (0x7A) and ID_X_HIGH_SCORE (0x7B) from the
// language's text table.
struct HudText {
  std::string score = "Score";
  std::string high_score = "Highscore";
};

// sub_8207CA48 for one player: the time limit's m:ss timer, multiplier and
// bombs along the top (Deadline), or lives and bombs either side of the
// centre over the multiplier, or the multiplier alone when the mode starts
// without lives; score top left, high score top right; in UI units
// (640 x 360) as UI lines. (Sequence's level timer is not ported yet.)
void draw_hud(const Font& font, const HudText& text, const HudState& s, LineBatch& out);

}  // namespace geow2

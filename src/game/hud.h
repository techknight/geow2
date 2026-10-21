#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "game/font.h"

namespace geow2 {

// What the single-player HUD shows (the player record 0x8219D068 and the
// game clock).
struct HudState {
  int64_t score = 0;            // +0x28
  int64_t high_score = 0;       // the mode's best (local table)
  bool high_board = false;      // the value is a friends board's, shown as it is (not raised to the score)
  std::string high_label;       // its label instead of ID_X_HIGH_SCORE (a name), when not empty
  int multiplier = 1;           // +0x40
  int bombs = 0;                // +0x34
  int lives = 0;                // +0x30
  float lives_pulse = 0.0f;     // +0x58 (dat_8219D0C0)
  float bombs_pulse = 0.0f;     // +0x5C (dat_8219D0C4)
  int start_lives = 0;          // dat_8219D264: 0 hides the lives and bombs
  float clock = 0.0f;           // dat_8219CE68
  float length = 180.0f;        // dat_8219D258
  int lives_mode = 2;           // dat_8219D24C (2: the m:ss timer)
  int mode = 0;                 // dat_8219D254 (2: Sequence's level timer)
  float level_time = 30.0f;     // dat_8219D25C
  float level_timer = 0.0f;     // dat_8219CE6C
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
// (640 x 360) as UI lines. Sequence shows its level's seconds and
// hundredths left, with lives and bombs either side.
void draw_hud(const Font& font, const HudText& text, const HudState& s, LineBatch& out);

// sub_8207CA48's high score from a friends board (signed in, the board
// read): `rows` best first (name, score), `own` the player's row.
// Personal Best: the player's own best on the board, under their name.
// Next Friend: from the row above the player's (j = min(max(own - 1, 0),
// max(count - 1, 0))) up, the first with a score this game's `score` has
// not passed, else the top row. `rows` must not be empty.
struct BoardHigh {
  int64_t score = 0;
  std::string label;
};
BoardHigh board_high(const std::vector<std::pair<std::string, int64_t>>& rows, int own, int64_t score,
                     bool personal_best, const std::string& player);

// What the multiplayer HUD shows of a score record: the colour id of its
// first ship (0 none), its ships' player numbers, lives, multiplier and
// score.
struct MultiHudRecord {
  int colour = 0;
  std::vector<int> numbers;
  int lives = 0;
  int multiplier = 1;
  int64_t score = 0;
};
struct MultiHudState {
  std::vector<MultiHudRecord> records;
  bool show_lives = false;      // dat_8219D264 > 0 or dat_8219D26C bit 0 clear
  int lives_mode = 2;           // dat_8219D24C
  float length = 180.0f;        // dat_8219D258
  float clock = 0.0f;           // dat_8219CE68
  int mode = 0;                 // dat_8219D254
  float level_time = 30.0f;     // dat_8219D25C
  float level_timer = 0.0f;     // dat_8219CE6C
};

// sub_8207CA48 with two or more records: each record's row in its corner
// (left for even, right for odd; top for 0 and 1, below for 2 and 3) in
// its team colour: ship glyphs, lives, multiplier and score, and a crown
// for the sole leader (grey for those tied at the top); then Deadline's
// m:ss clock (no warning colour) or Sequence's level clock.
void draw_multiplayer_hud(const Font& font, const MultiHudState& s, LineBatch& out);

}  // namespace geow2

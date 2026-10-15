#pragma once

#include <array>
#include <cstdarg>
#include <string>
#include <vector>

#include "game/font.h"
#include "game/line_quads.h"

namespace geow2 {

using Rgba = std::array<float, 4>;

// The UI palette (object 0x821F4610, filled by sub_82090DA8 from the
// Ui.Colour.* variables: rgb times alpha, alpha 1).
enum UiSlot : int {
  kUiBackground = 0,
  kUiSelection,
  kUiNormal,
  kUiBright,
  kUiHighlight,
  kUiDisabled,
  kUiSelectionDisabled,
  kUiScrollBar,
  kUiLockedBackground,
  kUiLockedNormal,
  kUiLockedBackgroundPending,
  kUiLockedNormalPending,
  kUiMultiplayerRed,
  kUiMultiplayerBlue,
  kUiMultiplayerYellow,
  kUiMultiplayerGreen,
  kUiMultiplayerDisabled,
  kUiIntermissionSelection,
  kUiIntermissionUnplayed,
  kUiIntermissionSuccess,
  kUiIntermissionDied,
  kUiIntermissionOutOfTime,
  kUiGraphAxis,
  kUiGraphScore,
  kUiGraphMultiplier,
  kUiGraphLabel,
  kUiSlotCount
};

struct UiPalette {
  std::array<Rgba, kUiSlotCount> slot{};
  // The palette at the colour variables' defaults.
  static UiPalette defaults();
  const Rgba& operator[](int i) const { return slot[size_t(i)]; }
};

Rgba scaled(const Rgba& c, float k);  // all four channels
Rgba scaled_rgb(const Rgba& c, float k);  // rgb only

// A vertex of the UI fill batch (0x821A5158): position in UI units and a
// colour.
struct UiFillVertex {
  float x, y;
  float r, g, b, a;
};

// What a UI frame draws: lines (the UI line batches) and filled quads,
// with the UI fade (dat_821993D0) the panels apply.
struct UiCanvas {
  LineBatch lines;
  std::vector<UiFillVertex> fill;  // four vertices per quad
  float fade = 1.0f;
  void clear() {
    lines.clear();
    fill.clear();
  }
};

// sub_82091FF8: a panel from (x0, y0) to (x1, y1) in `colour` (times the
// fade): edges cut into pieces brightest at the screen centre, an opaque
// fill and a faint grid inside. Flags: 1 no fill and no grid, 2 no left
// side, 4 no right side, 8 no top, 0x10 no bottom, 0x20 no grid.
void ui_panel(UiCanvas& c, float x0, float y0, float x1, float y1, const Rgba& colour, uint32_t flags);

// sub_82092478(y0, y1): a full-width panel in the background colour
// without sides.
void ui_bar(UiCanvas& c, const UiPalette& p, float y0, float y1);

// A text layout (sub_82076778 and its field helpers): up to 24 fields,
// each a text, a colour, a width scale, padding, an alignment and a
// minimum width, in a 0x200-byte pool.
class TextLayout {
 public:
  struct Field {
    std::string text;
    bool has_text = false;
    Rgba colour{1, 1, 1, 1};
    float scale = 1.0f;
    float pad_before = 0.0f, pad_after = 0.0f;
    int align = 0;
    float min_width = 0.0f;
  };
  // sub_82076E40 / sub_82076F40: a new field (null when 24 exist).
  Field* add(const Rgba& colour, const std::string& text);
  // sub_82076DD0: an empty field.
  Field* add_empty();
  // sub_82076FB8: fields split at '~', alternating the two colours.
  void add_split(const Rgba& a, const Rgba& b, const std::string& text);
  // sub_820767F0: every field's colour.
  void set_colour(const Rgba& c);
  // sub_82076840: every field's colour = add + mul * colour.
  void transform(const Rgba& add, const Rgba& mul);
  // sub_82076B18: the total width at width scale w.
  float width(const Font& font, float w) const;
  // sub_82076BB0: drawn at (x, y), squeezed to maxW (when >= 0); align 0
  // left, 1 right, 2 centre.
  void draw(const Font& font, LineBatch& out, float x, float y, float w, float h, float max_w, int align) const;
  bool empty() const { return fields_.empty(); }
  std::vector<Field>& fields() { return fields_; }

 private:
  std::vector<Field> fields_;
  size_t used_ = 0;
};

// sub_820768D0: word wrap of `text` at width scale w within max_w; returns
// the lines (at most `max_lines`).
std::vector<std::string> wrap_text(const Font& font, const std::string& text, float w, float max_w, int max_lines = 32);
// sub_820773B8: the height a text box would take.
float text_box_height(const Font& font, const std::string& text, float w, float h, float gap, float max_w);
// sub_820770F8: a wrapped text box at (x, y); h_align 0 left, 1 right, 2
// centre, 4 justified; v_align 0 top, 1 centre, 2 bottom; '[' and ']'
// switch to the highlight colour and back. Returns its height.
float text_box(const Font& font, LineBatch& out, const UiPalette& p, float x, float y, float w, float h, float gap,
               float max_w, int h_align, int v_align, const Rgba& colour, const std::string& text);

// printf into a std::string (the CRT's _vsnprintf_s with _TRUNCATE).
std::string format(const char* fmt, ...);

}  // namespace geow2

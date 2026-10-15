#pragma once

#include <vector>

#include "game/math.h"

namespace geow2 {

// One vertex of a line quad as sub_8205EB58 writes it (32 bytes): the
// position, a side marker (-1 or +1 across the line), the colour with its
// alpha folded in and scaled by 18, and an alpha slot (1, or 0 at the tip
// of an end cap).
struct LineVertex {
  float x, y, z, side;
  float r, g, b, a;
};

// The two batches every line goes into: the body quads (batch 0x821A3088)
// and the end caps (batch 0x821A38C8), four vertices per quad.
struct LineBatch {
  std::vector<LineVertex> body;
  std::vector<LineVertex> caps;
  void clear() {
    body.clear();
    caps.clear();
  }
};

// Render.LineWidth.Unit (variable 0x821A1D94, value at 0x821A1DB4, default
// dat_82000710): the half-width every line quad is offset by.
constexpr float kLineWidthUnit = 1.1f;

// sub_8205EB58(cap_size, p0, p1, colour): one body quad and two end-cap
// quads, computed as the original does (fused operations in double,
// rounded once on store). `width` is Render.LineWidth.Unit.
void emit_line(LineBatch& out, float width, float cap_size, const Vec4& p0, const Vec4& p1, const float colour[4]);

// sub_8205F6B8(p0, p1, colour0, colour1): the same quads with a colour per
// end and cap size 1 (the line debris, Render.Layer.Smash).
void emit_line2(LineBatch& out, float width, const Vec4& p0, const Vec4& p1, const float colour0[4],
                const float colour1[4]);

}  // namespace geow2

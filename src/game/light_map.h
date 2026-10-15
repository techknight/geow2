#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "game/grid.h"

namespace geow2 {

// A vertex of the grid's line quads and of its warp mesh (0x1C bytes):
// position, two values carried from the grid point (its texture-like
// coordinates), an alpha level and a side marker (sub_8205B880).
struct GridVertex {
  float x, y, z, u, v, alpha, side;
};

// A vertex of a light sprite (0x1C bytes, sub_8205FA98).
struct LightVertex {
  float x, y, u, v, r, g, b;
};

// The grid tuning variables (value address, default constant).
struct GridTuning {
  std::array<float, 8> alpha = {0.5f, 0.075f, 0.125f, 0.025f,
                                0.25f, 0.075f, 0.125f, 0.025f};  // Render.Grid.Alpha0..7, 0x821A1960..
  float brightness = 3.0f;    // Render.Grid.Brightness 0x821A1B00, dat_820006F8
  float line_width = 0.84375f;  // Render.Grid.LineWidth 0x821A1B34, dat_82002868
  float depth = -6.0f;        // Render.Grid.Depth 0x821A1B68, dat_82002884
};

// The grid's line quads (four vertices each) as the line pass in
// sub_8205B880 writes them: interior lines only, from the positions the
// last step read, half-width line_width * fade.
void grid_lines(const Grid& grid, const GridTuning& t, float fade, std::vector<GridVertex>& out);

// The warp mesh: one vertex per grid point (displacement from rest,
// depth, texture-like coordinates), row by row.
void grid_warp_mesh(const Grid& grid, const GridTuning& t, std::vector<GridVertex>& out);

// Quad-list indices of the warp mesh (sub_8205C7D0): 88 x 132 cells.
std::vector<uint16_t> grid_warp_indices();

// The grid's fade (dat_821A170C), eased toward 1 while in game
// (sub_8205CAF0); the grid is drawn while it is above 0.01.
float grid_fade_step(float fade, bool in_game);
constexpr float kGridVisible = 0.01f;  // dat_820011B0

// The arena walls (sub_8205CF20): for each of the four sides
// (sub_8205B000 adds them clockwise from the left one) a quad along the
// wall and one more scaled outward by Render.Wall.OuterScale, each a
// strip 1.5 either side of the line and 0.5 past its ends, carrying
// light-map coordinates up to 200 units across the wall (sub_8205CC70).
// Vertices as the grid's: (x, y, depth, u, v, 1, side).
constexpr float kWallOuterScale = 1.01f;  // Render.Wall.OuterScale 0x821A1B7C, dat_820028A0
void wall_quads(float half_x, float half_y, float depth, float outer_scale, std::vector<GridVertex>& out);

// sub_8205FA98: a light sprite, a square of half-size `size` around
// (x, y) with texture coordinates 0..1, colour times 1.5.
void emit_light(std::vector<LightVertex>& out, float size, float x, float y, const float colour[3]);

// The light sprite texture (sub_8205E408): 31 x 31 8-bit luminance, a
// normal density over +/- 3.141.
constexpr int kLightSpriteSize = 31;
std::vector<uint8_t> light_sprite_texture();

}  // namespace geow2

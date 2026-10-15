#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "game/player_motion.h"

namespace geow2 {

// The background grid: an 89 x 133 spring mesh over the arena
// (sub_8205B000 resets it, sub_8205B578 gathers what pushes on it,
// sub_8205B880 simulates it). Visual only.
struct GridLight {
  float x, y, strength, scale;  // sub_8205B578: (pos, 4.0 or 16.0, (+0x94 + 1) * 0.5 or -0.78125)
};

struct GridPush {
  float x, y, radius, strength;
};

struct GridInputs {
  std::vector<GridLight> lights;           // at most 32 used
  std::optional<Vec2> fading_player;       // pulls (factor 12)
  std::optional<Vec2> special;             // pushes away (factor 3.5)
  std::vector<std::vector<GridPush>> pushes;  // groups in gather order; each stops at the record limit
};

class Grid {
 public:
  static constexpr int kRows = 89;
  static constexpr int kCols = 133;

  void reset(float half_x, float half_y);
  void step(const GridInputs& in, float half_x, float half_y);

  Vec2 position(int row, int col) const { return pos_[cur_][index(row, col)]; }
  // The buffer the last step read from: what the original's line pass and
  // warp mesh use (they run while other threads write the new positions).
  Vec2 previous_position(int row, int col) const { return pos_[1 - cur_][index(row, col)]; }
  Vec2 rest(int row, int col) const { return rest_[index(row, col)]; }
  Vec2 uv(int row, int col) const { return uv_[index(row, col)]; }

 private:
  static int index(int row, int col) { return row * kCols + col; }
  struct Record {
    int cell;
    float x, y, radius2, strength;
  };
  bool add_push(const GridPush& p);

  std::vector<Vec2> rest_, uv_, vel_;
  std::vector<Vec2> pos_[2];
  int cur_ = 0;
  std::vector<Record> records_;
  std::vector<int> cell_start_;  // per cell [start[c], start[c + 1])
};

}  // namespace geow2

#pragma once

#include <vector>

#include "game/effects.h"
#include "game/light_map.h"
#include "game/line_quads.h"
#include "game/math.h"
#include "game/model_geometry.h"
#include "game/rng.h"

namespace geow2 {

// Smash tuning variables (registration address, default).
struct SmashTuning {
  float spring = 30.0f;  // Smash.SpringStrength (0x821B46EC, dat_82000CDC)
  float damp = 0.99f;    // Smash.Damp (0x821B4720, dat_82001BB4)
};

// One line of debris (0x5C bytes): both ends and their velocities (four
// components each), the rest length it springs back to, its life and
// colour.
struct DebrisPiece {
  Vec4 p0, v0, p1, v1;
  float length = 0.0f;
  float life = 0.0f, life_total = 0.0f;
  float colour[4] = {0, 0, 0, 0};
};

// The line debris of a destroyed model (Render.Layer.Smash): a pool of 1024
// pieces (dat_821B46E8, sub_82074A08). Each piece is a line of the model,
// flung outward, bouncing off the arena walls (only x and y) and keeping
// its length like a spring, fading over its life.
class Debris {
 public:
  static constexpr size_t kCapacity = 0x400;

  // sub_82074F58(speed, model, &impact, &killer velocity, matrix,
  // &colour): the lines of the model's first part, through `m`, from the
  // impact point; slow killers (under 200 a second) burst them outward
  // from the model's origin instead.
  void smash(Effects& fx, GameRng& rng, float speed, const std::vector<ModelLine>& lines, const Mat4& m,
             float impact_x, float impact_y, float killer_vx, float killer_vy, const float colour[4]);
  // sub_82074B28 (render worker): moves, bounces, springs and ages every
  // piece; spent pieces are replaced by the last one.
  void step(const SmashTuning& t, float dt, float half_x, float half_y);
  // sub_82074600: each piece as a line brighter at its nearer end, fading
  // over its last second, and a light sprite at its middle.
  void draw(LineBatch& out, std::vector<LightVertex>& lights) const;

  size_t size() const { return pieces_.size(); }

 private:
  // sub_82074860.
  void add(const Vec4& p0, const Vec4& v0, const Vec4& p1, const Vec4& v1, const float colour[4], GameRng& rng);

  std::vector<DebrisPiece> pieces_;
};

}  // namespace geow2

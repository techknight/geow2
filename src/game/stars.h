#pragma once

#include <vector>

#include "game/game_math.h"
#include "game/rng.h"

namespace geow2 {

// One background star (0x14 bytes at 0x821B4770 + i * 0x14).
struct Star {
  float x, y;    // position, spread wider the nearer the star
  float near;    // [0, 1), cubed: how near the star is
  float phase;   // twinkle phase, counts down and restarts at 1
  float depth;   // z, from -3750 (far) to -750
};

// A star as drawn (6 floats, sub_82075548): position and colour.
struct StarVertex {
  float x, y, z, r, g, b;
};

// The background starfield (Render.Layer.Stars): 6000 stars made at
// start-up from the game's random generator (sub_82075318), twinkling
// and fading in each step (sub_82075548), drawn as point sprites
// (sub_820756F8).
class StarField {
 public:
  static constexpr int kCount = 6000;

  void init(GameRng& rng);
  // One step: fade in, advance each star's twinkle, write the vertices.
  // `zoom` is dat_821825EC (1.0 in game).
  void update(const GameMath& gm, float zoom, std::vector<StarVertex>& out);

  // dat_821B4758: 0 while the intro runs, then rises 0.03 a step to 1.
  // A game starts long after the intro, so it begins at 1.
  float fade = 1.0f;
  std::vector<Star> stars;
};

}  // namespace geow2

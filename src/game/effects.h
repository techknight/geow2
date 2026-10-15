#pragma once

#include <cmath>

#include "game/game_math.h"
#include "game/particles.h"
#include "game/rng.h"

namespace geow2 {

// Effect tuning variables (value address, default).
struct EffectTuning {
  float factor = 0.8f;          // Effect.Factor 0x82199288, dat_82000754
  float size = 1.25f;           // Effect.Size 0x821992BC, dat_82000768
  float wheel_radius = 600.0f;  // Effect.ColourWheelRadius 0x82199324, dat_8200078C
  float wheel_offset = 600.0f;  // Effect.ColourWheelOffset 0x82199358, dat_8200078C
  float wheel_rate = 2.0f;      // Effect.ColourWheelRate 0x8219938C, dat_820006FC
};

// A unit's killed effect (Data.bin EffectExplosion / EffectRandomExplosion).
struct UnitEffect {
  enum Kind { kNone, kExplosion, kRandomExplosion } kind = kNone;
  int count = 0;
  float radius = 0.0f;
  float colour0[4] = {0, 0, 0, 0};
  float colour1[4] = {0, 0, 0, 0};
};

// The six colours of the wheel (0x82180728: red, green and blue rows).
struct EffectColours {
  // sub_820304C8(alpha): one of the six at random (r % 6).
  static void random(GameRng& rng, float alpha, float out[4]);
};

class Effects {
 public:
  Effects(const GameMath& gm, GameRng& rng, Particles& particles) : gm_(gm), rng_(rng), particles_(particles) {}

  // sub_82030A98: the wheel turns by Effect.ColourWheelRate a second.
  void step(float dt) { wheel_phase_ = std::fmaf(tuning.wheel_rate, dt, wheel_phase_); }
  // sub_82030678: the colour at point `p` (x, y, z, w) among the wheel's
  // six colours (inverse-square weights), turned by `t` and the wheel's
  // phase. Explosions pass their position: their colours depend on where
  // they happen.
  void wheel(float t, const float p[4], float out[4]) const;
  // sub_82030270: a random point in the cube of half side `s`: z, y, x
  // drawn in that order, each (r * 2^-31 - 1) * s; w = 0.
  void random_cube(float s, float out[4]);
  // sub_82030168: a random point in a ball of radius `r` (4 components).
  void random_in_ball(float r, float out[4]);
  // sub_82030C58(radius, &pos, count, ...): a burst of particles at (x, y)
  // in the wheel's colours there. (Its callers also pass colours in r6/r7,
  // which it never reads.)
  void explosion(float radius, float x, float y, int count);
  // sub_82030658: a unit's killed effect.
  void trigger(const UnitEffect& e, float x, float y);

  EffectTuning tuning;

 private:
  const GameMath& gm_;
  GameRng& rng_;
  Particles& particles_;
  float wheel_phase_ = 0.0f;  // dat_82199260
};

}  // namespace geow2

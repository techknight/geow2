#pragma once

#include <vector>

#include "game/light_map.h"
#include "game/line_quads.h"

namespace geow2 {

// A particle (0x2C bytes in the original's pool).
struct Particle {
  float x, y, z;
  float vx, vy, vz;
  float r, g, b;
  float intensity;  // +0x24: 4.0 at birth, falls by the step; dead below 0
  float speed;      // +0x28: smoothed |velocity|
};

// A point the particles swirl around (gravity wells, sys +0x30) or are
// pushed from (sys +0xF0, with a strength at sys +0x1A8).
struct ParticleAttractor {
  float x, y, z;
};
struct ParticleRepulsor {
  float x, y, z, strength;
};

// Particle tuning variables (value address, default).
struct ParticleTuning {
  float damp = 0.965f;        // Particle.Damp 0x821B3EA0 (default at 0x82002594)
  float line_width = 0.5f;    // Particle.LineWidth 0x821B3EF8, dat_82000724
  float line_height = 0.5f;   // Particle.LineHeight 0x821B3F2C, dat_82000724
};

// The particle pool (dat_821B350C, 256 blocks of 128: sub_82063EC0) and
// its step (render workers: sub_82063A38 / sub_82063448).
class Particles {
 public:
  static constexpr size_t kCapacity = 256 * 128;

  // sub_82062FC8: no room for another particle. The original's limit is
  // per block and depends on its worker threads (DIFFERENCES.md).
  bool full() const { return live_.size() >= kCapacity; }
  // sub_82063100 (via sub_82063EF0): a new particle, intensity 4.0
  // (dat_820006F4).
  void add(float x, float y, float z, float vx, float vy, float vz, const float colour[3]);

  // One step of every live particle, in order, dropping the dead: moves by
  // velocity * Particle.Damp, mirrors off the arena edges, swirls around
  // attractors and is pushed by repulsors, smooths its speed and fades;
  // writes its streak quad (four line vertices, from where it was to where
  // it is) and its light sprite (four vertices, size 15 at the middle).
  void step(const ParticleTuning& t, float dt, float half_x, float half_y,
            const std::vector<ParticleAttractor>& attractors, const std::vector<ParticleRepulsor>& repulsors,
            std::vector<LineVertex>& quads, std::vector<LightVertex>& lights);

  size_t size() const { return live_.size(); }

 private:
  std::vector<Particle> live_;
};

}  // namespace geow2

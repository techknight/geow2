#pragma once

#include <cstdint>

#include "game/world.h"

namespace geow2 {

// The spawner of Evolved, Waves, Pacifism and King (sub_8203FFB8; state at
// 0x8219DDA0, set up by sub_8203EC48 at every game start): a timer to the
// next wave, and each mode's wave.
class WaveSpawner {
 public:
  // sub_8203EC48.
  void reset(World& w);
  // One step for mode `mode` (1 Evolved, 3 Waves, 4 Pacifism, 5 King),
  // while a player is alive. `cycle_refresh` is sub_8203EB88's test:
  // multiplayer, or a bomb used, or the shield hit (dat_8219D0C8 / C9).
  void step(World& w, int mode, bool cycle_refresh);

  // dat_8219D288: 1 when the last player died; the next wave then comes
  // three seconds after the respawn (2 while that wait runs).
  int after_death = 0;
  int walls = 0;  // dat_8219CE80: rocket walls so far (an achievement counts them)

  // The state block.
  int side[4] = {0, 0, 0, 0};          // +0x00: wall sides in cycle order
  bool full[4] = {false, false, false, false};  // +0x10: full-wall flags
  int index = 5;                       // +0x14 (5: the cycle not yet filled)
  bool flip = false;                   // +0x18: which half of a wall
  bool current_full = false;           // +0x19
  float progress = 0.0f;               // +0x1C: waves so far (Evolved: 0.7 each)
  float timer = 2.0f;                  // +0x20: seconds to the next wave
  int state = 0;                       // +0x24: Evolved's sub-state
  int group_type = 2;                  // +0x28
  int group_count = 0;                 // +0x2C

 private:
  void new_cycle(World& w);  // sub_8203E910
  int next_side(World& w, bool refresh);  // sub_8203EB88
  void evolved(World& w, bool refresh);
  void evolved_start(World& w);    // sub_8203ECC0
  void evolved_corners(World& w);  // sub_8203F938
  void evolved_rings(World& w);    // sub_8203FB10
  void evolved_swarm(World& w);    // sub_8203FCF0
  void pacifism(World& w);
  void waves(World& w, bool refresh);
  void king(World& w);
};

}  // namespace geow2

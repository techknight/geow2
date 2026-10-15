#pragma once

#include "game/world.h"

namespace geow2 {

// Deadline's enemy spawner (sub_8203DED0; state at 0x8219DB48, reset by
// sub_8203E538). Every few seconds a wave spends a budget that grows from
// the last one, on groups whose kind follows the budget left.
class DeadlineSpawner {
 public:
  // One step; `elapsed` is the game time so far (dat_8219CE68).
  void step(World& w, float elapsed);
  // The bomb's effect on the next waves (sub_8203E4B0).
  void bombed();

  float wave_timer = 0.0f;   // DB48: seconds to the next wave
  float start_delay = 1.0f;  // DB4C: seconds before the first wave
  int db50 = 0, db54 = 0;    // budget adjustments (raised elsewhere)
  int budget = 0;            // DB58: the last wave's budget
  int waves = 0;             // DB5C
  int well_wait = 0;         // DB60: waves until a gravity well may come
  int gate_wait = 0;         // DB64: waves until gates may come
  int total = 0;             // DB70

 private:
  void group(World& w, int type, int& placed_left);
};

constexpr float kDeadlineLength = 180.0f;  // dat_82000CD8 (sub_82034EA0)

}  // namespace geow2

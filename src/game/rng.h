#pragma once

#include <cstdint>
#include <random>

namespace geow2 {

// The game's one random generator (state at 0x821A1E28): the reference
// MT19937, seeded and stepped exactly as std::mt19937 does it
// (sub_82030010 seeds, sub_82030058 returns the next 32-bit value).
class GameRng {
 public:
  // sub_82037C20 seeds with (tick count & 0x7FFFFFFF) * 2 + 1.
  void seed_from_ticks(uint64_t ticks) { mt_.seed(uint32_t((ticks & 0x7FFFFFFF) * 2 + 1)); }
  void seed(uint32_t s) { mt_.seed(s); }
  uint32_t next() { return uint32_t(mt_()); }

 private:
  std::mt19937 mt_;
};

}  // namespace geow2

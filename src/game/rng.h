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

// The C runtime's rand() (sub_8212FBB8): the per-thread holdrand starts at
// 1 (sub_82131228) and nothing calls srand. Only Evolved's spawner uses it.
class CrtRand {
 public:
  int next() {
    hold_ = hold_ * 214013u + 2531011u;
    return int((hold_ >> 16) & 0x7FFF);
  }

 private:
  uint32_t hold_ = 1;
};

}  // namespace geow2

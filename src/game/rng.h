#pragma once

#include <cstdint>

namespace geow2 {

// The game's one random generator (state at 0x821A1E28): the reference
// MT19937 laid out as the original keeps it, 624 words and the index of
// the next one (word 0x270), so a state read from the console's memory
// can be loaded (sub_82030010 seeds, sub_82030058 returns the next 32-bit
// value).
class GameRng {
 public:
  static constexpr int kWords = 624;
  // sub_82037C20 seeds with (tick count & 0x7FFFFFFF) * 2 + 1.
  void seed_from_ticks(uint64_t ticks) { seed(uint32_t((ticks & 0x7FFFFFFF) * 2 + 1)); }
  void seed(uint32_t s) {
    state_[0] = s;
    for (int i = 1; i < kWords; ++i)
      state_[size_t(i)] = (state_[size_t(i - 1)] ^ (state_[size_t(i - 1)] >> 30)) * 0x6C078965u + uint32_t(i);
    index_ = kWords;
  }
  uint32_t next() {
    if (index_ >= kWords) twist();
    uint32_t y = state_[size_t(index_++)];
    y ^= y >> 11;
    y ^= (y << 7) & 0x9D2C5680u;
    y ^= (y << 15) & 0xEFC60000u;
    y ^= y >> 18;
    return y;
  }
  // The index of the next word and a state word (for comparing with the
  // original's memory).
  int index() const { return index_; }
  uint32_t word(int i) const { return state_[size_t(i)]; }
  // The state as the original stores it.
  void load(const uint32_t (&words)[kWords], int index) {
    for (int i = 0; i < kWords; ++i) state_[size_t(i)] = words[i];
    index_ = index;
  }

 private:
  void twist() {
    static constexpr uint32_t kMag[2] = {0, 0x9908B0DFu};  // dat_820006BC
    for (int i = 0; i < kWords; ++i) {
      const uint32_t y = (state_[size_t(i)] & 0x80000000u) | (state_[size_t((i + 1) % kWords)] & 0x7FFFFFFFu);
      state_[size_t(i)] = state_[size_t((i + 397) % kWords)] ^ (y >> 1) ^ kMag[y & 1];
    }
    index_ = 0;
  }

  uint32_t state_[kWords] = {};
  int index_ = kWords;
};

// The C runtime's rand() (sub_8212FBB8): the per-thread holdrand starts at
// 1 (sub_82131228) and nothing calls srand. Only Evolved's spawner uses it.
class CrtRand {
 public:
  int next() {
    hold_ = hold_ * 214013u + 2531011u;
    return int((hold_ >> 16) & 0x7FFF);
  }
  // The state; it runs on across games for the whole session.
  uint32_t state() const { return hold_; }
  void set_state(uint32_t h) { hold_ = h; }

 private:
  uint32_t hold_ = 1;
};

}  // namespace geow2

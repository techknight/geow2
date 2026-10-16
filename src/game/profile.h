#pragma once

#include <array>
#include <cstdint>

namespace geow2 {

// Structure.KillsToUnlock (0x8219943C, value 0x8219945C): progress points
// a mode needs to unlock the next one.
constexpr int kKillsToUnlock = 400;

// The modes' "profile index" (sub_820933D8 / sub_82093448): 0 Evolved, 1
// Deadline, 2 Pacifism, 3 Waves, 4 King, 5 Sequence.
int profile_index(int mode);
// sub_820934B8: the inverse.
int mode_of_index(int index);
// sub_82093598: the index whose progress unlocks this one (-1: Deadline).
int prerequisite(int index);

// The saved settings and progress of one player (0x30 bytes per pad at
// 0x821F47B8, saved verbatim as profile setting 0x63E83FFF), defaults as
// sub_82093230 sets them.
struct Profile {
  uint32_t controls = 2;     // +0x00: bit 0 move on the right stick, bit 1 fire on the right stick, bit 2 bomb on the bumpers
  float sound_volume = 1.0f;   // +0x04
  float music_volume = 1.0f;   // +0x08
  int32_t score_display = 0;   // +0x0C: 0 next friend, 1 personal best
  uint32_t unlocked = 2;       // +0x10: bit per index (Deadline's set)
  std::array<int32_t, 6> progress{};  // +0x14: per index
  uint32_t millions = 0;       // +0x2C: scored over 1,000,000, bit per index

  // sub_82093640: whether a mode can be played (Deadline always; in a
  // trial only Deadline).
  bool mode_unlocked(int mode, bool trial = false) const;
  // sub_82093748: unlocked and never progressed.
  bool index_new(int index) const;

  // The 48-byte block, big-endian as the console stores it.
  std::array<uint8_t, 0x30> to_bytes() const;
  static Profile from_bytes(const std::array<uint8_t, 0x30>& b);
  // A block from the port's own file (the console's profile is signed,
  // this file is not): settings outside the ranges their menus allow go
  // back to the defaults and Deadline stays unlocked. Returns whether
  // anything was repaired.
  bool repair();

  // sub_820338D8 at a single-player game over (not trial): unlock progress
  // from the kills, the next mode at the threshold, the million bit.
  // Returns whether anything changed (the profile is then saved).
  bool game_ended(int mode, int kills, int64_t score);
};

}  // namespace geow2

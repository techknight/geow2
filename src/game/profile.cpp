#include "game/profile.h"

#include <cstring>

namespace geow2 {
namespace {

void put32(std::array<uint8_t, 0x30>& b, size_t o, uint32_t v) {
  b[o] = uint8_t(v >> 24);
  b[o + 1] = uint8_t(v >> 16);
  b[o + 2] = uint8_t(v >> 8);
  b[o + 3] = uint8_t(v);
}

uint32_t get32(const std::array<uint8_t, 0x30>& b, size_t o) {
  return uint32_t(b[o]) << 24 | uint32_t(b[o + 1]) << 16 | uint32_t(b[o + 2]) << 8 | uint32_t(b[o + 3]);
}

uint32_t float_bits(float f) {
  uint32_t u;
  std::memcpy(&u, &f, 4);
  return u;
}

float bits_float(uint32_t u) {
  float f;
  std::memcpy(&f, &u, 4);
  return f;
}

}  // namespace

int profile_index(int mode) {
  switch (mode) {
    case 0: return 1;
    case 1: return 0;
    case 2: return 5;
    case 3: return 3;
    case 4: return 2;
    case 5: return 4;
    default: return -1;
  }
}

int mode_of_index(int index) {
  switch (index) {
    case 0: return 1;
    case 1: return 0;
    case 2: return 4;
    case 3: return 3;
    case 4: return 5;
    case 5: return 2;
    default: return -1;
  }
}

int prerequisite(int index) {
  switch (index) {
    case 0: return 4;
    case 2: return 0;
    case 3: return 2;
    case 4: return 1;
    case 5: return 3;
    default: return -1;
  }
}

bool Profile::mode_unlocked(int mode, bool trial) const {
  if (trial) return mode == 0;
  switch (mode) {
    case 0: return true;
    case 1: return (unlocked & 1) != 0;
    case 2: return (unlocked >> 5 & 1) != 0;
    case 3: return (unlocked >> 3 & 1) != 0;
    case 4: return (unlocked >> 2 & 1) != 0;
    case 5: return (unlocked >> 4 & 1) != 0;
    default: return false;
  }
}

bool Profile::index_new(int index) const {
  if (index < 0 || index > 5) return false;
  return mode_unlocked(mode_of_index(index)) && progress[size_t(index)] == 0;
}

std::array<uint8_t, 0x30> Profile::to_bytes() const {
  std::array<uint8_t, 0x30> b{};
  put32(b, 0x00, controls);
  put32(b, 0x04, float_bits(sound_volume));
  put32(b, 0x08, float_bits(music_volume));
  put32(b, 0x0C, uint32_t(score_display));
  put32(b, 0x10, unlocked);
  for (size_t i = 0; i < 6; ++i) put32(b, 0x14 + 4 * i, uint32_t(progress[i]));
  put32(b, 0x2C, millions);
  return b;
}

Profile Profile::from_bytes(const std::array<uint8_t, 0x30>& b) {
  Profile p;
  p.controls = get32(b, 0x00);
  p.sound_volume = bits_float(get32(b, 0x04));
  p.music_volume = bits_float(get32(b, 0x08));
  p.score_display = int32_t(get32(b, 0x0C));
  p.unlocked = get32(b, 0x10);
  for (size_t i = 0; i < 6; ++i) p.progress[i] = int32_t(get32(b, 0x14 + 4 * i));
  p.millions = get32(b, 0x2C);
  return p;
}

bool Profile::game_ended(int mode, int kills, int64_t score) {
  const int x = profile_index(mode);
  bool changed = false;
  if (x >= 0) {
    // Three points a kill up to 50, then each kill counts half as much
    // past 50, 100, 200 and 300 (signed halving, truncating).
    int gain;
    if (kills <= 50) {
      gain = 3 * kills;
      if (gain < 0) gain = 0;
      if (gain > 50) gain = 50;
    } else {
      gain = (kills - 50) / 2 + 50;
      if (gain > 100) gain = (gain - 100) / 2 + 100;
      if (gain > 200) gain = (gain - 200) / 2 + 200;
      if (gain > 300) gain = (gain - 300) / 2 + 300;
      if (gain < 0) gain = 0;
    }
    const int old = progress[size_t(x)];
    int now = old + gain;
    if (now < 0) now = 0;
    if (now > kKillsToUnlock) now = kKillsToUnlock;
    progress[size_t(x)] = now;
    if (uint32_t(now) < uint32_t(kKillsToUnlock)) {
      changed = now != old;
    } else {
      // The next mode in the chain (sub_82033430).
      static const int kNext[6] = {2, 4, 3, 5, 0, -1};
      const int bit = kNext[x];
      if (bit >= 0 && !(unlocked & (1u << bit))) {
        unlocked |= 1u << bit;
        changed = true;
      }
    }
    if (score > 1000000) {
      static const uint32_t kBits[6] = {1, 2, 4, 8, 0x10, 0x20};
      if (!(millions & kBits[x])) {
        millions |= kBits[x];
        changed = true;
      }
    }
  }
  return changed;
}

}  // namespace geow2

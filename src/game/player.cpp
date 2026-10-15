#include "game/player.h"

namespace geow2 {
namespace {

// dat_821A1D64: the steps per second (60).
constexpr int kStepsPerSecond = 60;

// int(60 * t) mod 10, as fctiwz and the compiler's division give it.
int blink_phase(float t) { return int(float(kStepsPerSecond) * t) % 10; }

}  // namespace

float trigger_axis(uint8_t raw) { return float(raw) * 0.00392156886f; }  // dat_82000E18

void player_spawned(PlayerLife& p) {
  p.present = true;
  p.removed = false;
  p.fading = true;
  p.fade = 0.0f;
  p.shield = 3.33f;  // dat_82001E94
  p.bomb_wait = 0.0f;
  p.shield_shown = true;
  p.bomb_latch = false;
}

void player_timers(PlayerLife& p, float step) {
  if (0.0f < p.shield) {
    p.shield = p.shield - step;
    // (0 < t < 1 and int(60 t) mod 10 == 4: sound event 8.)
  }
  if (0.0f < p.bomb_wait) p.bomb_wait = p.bomb_wait - step;
  // Shown while t > 0, except over the last second when int(60 t) mod 10
  // is above 4 (dat_820006C4).
  const float t = p.shield;
  if (t <= 0.0f)
    p.shield_shown = false;
  else if (t <= 1.0f && blink_phase(t) > 4)
    p.shield_shown = false;
  else
    p.shield_shown = true;
}

bool player_bomb(PlayerLife& p, float trigger, const ModeRules& rules) {
  if (!(0 < rules.start_bombs || (rules.flags & 2) == 0)) return false;
  if (trigger <= 0.15f) {  // dat_82000D58
    p.bomb_latch = false;
    return false;
  }
  if (p.bomb_latch) return false;
  p.bomb_latch = true;
  if (!(p.bomb_wait <= 0.0f)) return false;
  p.bomb_wait = 0.2f;  // dat_82000D5C
  if (p.bombs <= 0) return false;  // sound event 0x35
  --p.bombs;
  return true;
}

}  // namespace geow2

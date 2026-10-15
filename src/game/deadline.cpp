#include "game/deadline.h"

#include <cmath>

#include "game/spawn.h"

namespace geow2 {
namespace {

constexpr float kFar = 165.0f;  // dat_82000EA8: spawn distance from the players

}  // namespace

void DeadlineSpawner::group(World& w, int type, int& placed_left) {
  // sub_8203DE08.
  if (type == 22) {
    spawn_at_random(w, 100.0f, 22);  // dat_82001020
    return;
  }
  if (type != 1 && placed_left > 0 && w.random_float(1.0f) < 0.5f) {
    const Placement p = placed_position(w);
    spawn_enemy(w, type, p.angle, p.x, p.y);
    --placed_left;
    return;
  }
  spawn_at_random(w, kFar, type);
}

void DeadlineSpawner::step(World& w, float elapsed) {
  if (!(elapsed < kDeadlineLength)) return;
  start_delay = start_delay - w.step;
  if (start_delay > 0.0f) return;
  // Waves come every 5 seconds, or within half a second once fewer than
  // two enemies are left.
  float t = wave_timer;
  if (0.5f < t && w.enemy_count < 2) t = 0.5f;
  wave_timer = t - w.step;
  if (wave_timer > 0.0f) return;

  // Budget: between the last one and 1.5 times it plus 5.
  // Raised by kills (DB50, sub_8203BAC8) and geoms (DB54, sub_8203BBB8).
  int n = (w.spawn_kills - int(float(w.spawn_geoms) * -0.5f)) + 2;
  const int hi = int(std::fmaf(float(budget), 1.5f, 5.0f));
  if (n < budget)
    n = budget;
  else if (hi < n)
    n = hi;
  total += n;

  // A gravity well every fifth wave above 45, gates every third above 25.
  if (well_wait < 1) {
    if (n > 45) {
      spawn_at_random(w, kFar, 5);
      well_wait = 4;
    }
  } else {
    --well_wait;
  }
  if (gate_wait < 1) {
    if (n > 25) {
      spawn_at_random(w, kFar, 16);
      gate_wait = 2;
    }
  } else {
    --gate_wait;
  }

  // Spend the budget; what the remainder (mod 80) leaves picks the group.
  bool combo = false, alt2 = false, alt3 = false, alt6 = false;
  int placed_left = 1, spent = 0, left = n;
  while (n > 0) {
    const int m = left % 80;
    if (m < 46) {
      if (m < 16) {
        if (m < 6) {
          group(w, alt3 ? 22 : 1, placed_left);
          spent += 1;
          alt3 = !alt3;
          left -= 1;
        } else {
          group(w, alt2 ? 2 : 12, placed_left);
          spent += 2;
          alt2 = !alt2;
          left -= 2;
        }
      } else {
        group(w, alt6 ? 4 : 3, placed_left);
        spent += 4;
        alt6 = !alt6;
        left -= 4;
      }
    } else if (combo) {
      group(w, alt6 ? 4 : 3, placed_left);
      group(w, alt2 ? 2 : 12, placed_left);
      group(w, alt3 ? 22 : 1, placed_left);
      alt6 = !alt6;
      alt2 = !alt2;
      alt3 = !alt3;
      spent += 8;
      combo = false;
      left -= 8;
    } else if (placed_left < 1 || !(w.random_float(1.0f) < 0.5f)) {
      spawn_at_random(w, kFar, 8);
      spent += 8;
      combo = true;
      left -= 8;
    } else {
      const Placement p = placed_position(w);
      spawn_enemy(w, 8, p.angle, p.x, p.y);
      spent += 8;
      left -= 8;
      --placed_left;
      combo = true;
    }
    if (n <= spent) break;
  }
  wave_timer = 5.0f;  // dat_820006F0
  w.spawn_kills = 0;
  w.spawn_geoms = 0;
  ++waves;
  budget = n;
  w.play(audio::Event::kNewSpawnGroup);
}

void DeadlineSpawner::player_died(World& w, float time_left) {
  w.spawn_kills = 0;
  wave_timer = 0.0f;
  w.spawn_geoms = 0;
  start_delay = 3.0f;  // dat_820006F8
  // dat_82000D50 (120), dat_82000CE0 (60), dat_82000CDC (30): shares
  // dat_82000FF4, dat_82001160, dat_82000724, dat_820010F0.
  float share;
  if (time_left > 120.0f)
    share = 0.666666687f;
  else if (time_left > 60.0f)
    share = 0.6f;
  else if (time_left > 30.0f)
    share = 0.5f;
  else
    share = 0.333333343f;
  int cut = int(float(budget) * share);
  if (cut > 30) cut = 30;
  budget = budget - cut;
  if (budget < 0) budget = 0;
}

void DeadlineSpawner::bombed(World& w) {
  // sub_8203E4B0: the wave comes at once, the budget drops by 30% (at
  // most 10).
  wave_timer = 0.0f;
  start_delay = 1.5f;
  w.spawn_kills = 0;
  w.spawn_geoms = 0;
  int cut = int(float(budget) * 0.3f);
  if (cut > 10) cut = 10;
  budget = budget - cut;
  if (budget < 0) budget = 0;
}

}  // namespace geow2

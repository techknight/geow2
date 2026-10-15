#include "game/wave_spawner.h"

#include <cmath>
#include <utility>

#include "game/spawn.h"

namespace geow2 {
namespace {

// The spawners' distances: 100 (dat_82001020) and 165 (dat_82000EA8).
constexpr float kNear = 100.0f;
constexpr float kFar = 165.0f;

// The timers' clamp (two fsel): x below `lo` gives lo, then x at or above
// `hi` gives hi. x is c - t * k with one rounding (fnmsubs).
float clamp_timer(float c, float t, float k, float lo, float hi) {
  const float x = std::fmaf(-t, k, c);
  const float a = lo - x < 0.0f ? x : lo;
  return x - hi < 0.0f ? a : hi;
}

// sub_82030430(1.0): a uniform draw in [0, 1).
float uniform(World& w) { return w.random_float(1.0f); }

// sub_8203FF18: units with flag 4 that are grunts, protons, spinners or
// snake heads (no removal test).
int king_blockers(const World& w) {
  int n = 0;
  for (const auto& e : w.entities) {
    if (!e->unit_based || !(e->flags & kEntityTouches)) continue;
    if (e->type == 2 || e->type == 4 || e->type == 9 || e->type == 0xD) ++n;
  }
  return n;
}

}  // namespace

void WaveSpawner::reset(World& w) {
  progress = 0.0f;
  state = 0;
  group_type = 2;
  group_count = 0;
  timer = 2.0f;  // dat_820006FC
  index = 5;
  flip = (w.rng.next() & 1) != 0;
  new_cycle(w);
}

void WaveSpawner::new_cycle(World& w) {
  flip = !flip;
  const int last = index < 5 ? side[3] : -1;
  for (int i = 0; i < 4; ++i) {
    side[i] = i;
    full[i] = false;
  }
  // The first k entries are full walls, k = int(t + 1.5) / 5 in [0, 4].
  int k = int(progress + 1.5f) / 5;  // dat_82000C90
  if (k < 0) k = 0;
  if (k > 4) k = 4;
  for (int i = 0; i < k; ++i) full[i] = true;
  for (uint32_t i = 0; i < 3; ++i) {
    const uint32_t j = w.rng.next() % (4 - i) + i;
    std::swap(side[i], side[j]);
  }
  for (uint32_t i = 0; i < 3; ++i) {
    const uint32_t j = w.rng.next() % (4 - i) + i;
    std::swap(full[i], full[j]);
  }
  if (index < 5) {
    // A new cycle never starts on the side the last one ended on.
    if (last == side[0]) {
      const uint32_t j = w.rng.next() % 3 + 1;
      std::swap(side[0], side[j]);
    }
    index = 0;
  } else {
    // The first: top or bottom first and third, left or right between.
    const bool a = w.rng.next() % 100 < 50;
    const bool b = w.rng.next() % 100 < 50;
    index = 0;
    side[0] = a ? 2 : 3;
    side[2] = a ? 3 : 2;
    side[1] = b ? 0 : 1;
    side[3] = b ? 1 : 0;
  }
}

int WaveSpawner::next_side(World& w, bool refresh) {
  if (index > 3) {
    if (refresh)
      new_cycle(w);
    else
      index = 0;
  }
  ++walls;
  current_full = full[index];
  return side[index++];
}

void WaveSpawner::step(World& w, int mode, bool cycle_refresh, int& after_death) {
  if (after_death == 1) {
    timer = 3.0f;  // dat_820006F8
    after_death = 2;
  }
  const int waiting = after_death;
  // Nothing left to fight: the next wave comes early (King: once no grunt,
  // proton, spinner or snake is left; others half a second, or at once
  // from wave 10).
  if (mode == 5) {
    if (0.0f < timer && king_blockers(w) == 0 && waiting == 0) timer = 0.0f;
  } else if (0.5f < timer && w.enemy_count == 0 && after_death == 0) {
    timer = 10.0f <= progress ? 0.0f : 0.5f;  // dat_8200070C, dat_82000724
  }
  timer = timer - w.step;
  if (timer <= 0.0f && after_death == 2) after_death = 0;
  if (0.0f <= timer) return;
  switch (mode) {
    case 1: evolved(w, cycle_refresh); break;
    case 3: waves(w, cycle_refresh); break;
    case 4: pacifism(w); break;
    case 5: king(w); break;
    default: break;  // modes 6 and 7: unreachable
  }
}

void WaveSpawner::evolved(World& w, bool refresh) {
  switch (state) {
    case 0: evolved_start(w); break;
    case 1: evolved_corners(w); break;
    case 2: evolved_rings(w); break;
    case 3: evolved_swarm(w); break;
    case 4: {
      // sub_8203FE88: a wall of rockets, half of one before t = 100 (the
      // half read before the side is drawn).
      if (100.0f <= progress) {
        spawn_wall(w, 12, next_side(w, refresh), false);
      } else {
        const bool half = flip;
        spawn_half_wall(w, 12, next_side(w, refresh), half);
      }
      timer = timer * 30.0f;  // dat_82000CDC
      state = 0;
      break;
    }
    default: break;
  }
}

void WaveSpawner::evolved_start(World& w) {
  const float t0 = progress;
  const float t = t0 + 0.7f;  // dat_82001190
  progress = t;
  timer = clamp_timer(2.0f, t0, 0.02f, 0.275f, 2.0f);  // dat_82001194, dat_82000D08
  if (t < 2.0f) {
    spawn_at_random(w, kNear, 1);
    spawn_at_random(w, kNear, 22);
    spawn_at_random(w, kNear, uniform(w) < 0.8f ? 22 : 12);  // dat_82000754
    timer = timer + 3.0f;
    return;
  }
  if (t < 5.0f) {
    spawn_at_random(w, kNear, 22);
    spawn_at_random(w, kNear, 22);
    spawn_at_random(w, kNear, 22);
    spawn_at_random(w, kNear, uniform(w) < 0.5f ? 22 : 12);
    spawn_at_random(w, kFar, 2);
    spawn_at_random(w, kFar, 1);
    spawn_at_random(w, kFar, 1);
    timer = timer + 1.5f;
    return;
  }
  if (t < 10.0f) {
    const float u = uniform(w);
    spawn_at_random(w, kFar, u < 0.5f ? 2 : (u < 0.75f ? 3 : 4));  // dat_82000718
    spawn_at_random(w, kFar, 1);
    spawn_at_random(w, kNear, 22);
    return;
  }
  // A group by a draw (0.24, 0.7, 0.8, 0.95: dat_82001178, dat_82001190,
  // dat_82000754, dat_82001174).
  {
    const float u = uniform(w);
    if (u < 0.24f) {
      spawn_at_random(w, kNear, 22);
      spawn_at_random(w, kNear, 1);
      spawn_at_random(w, kFar, 2);
    } else if (u < 0.7f) {
      spawn_at_random(w, kNear, 1);
      spawn_at_random(w, kNear, 1);
      spawn_at_random(w, kFar, 12);
    } else if (u < 0.8f) {
      spawn_at_random(w, kNear, 2);
      spawn_at_random(w, kNear, 2);
      spawn_at_random(w, kFar, 16);
    } else if (u < 0.95f) {
      spawn_at_random(w, kNear, 22);
      spawn_at_random(w, kFar, 12);
      spawn_at_random(w, kFar, 4);
    } else {
      spawn_at_random(w, kNear, 22);
      spawn_at_random(w, kNear, 22);
      spawn_at_random(w, kFar, 5);
    }
  }
  if (uniform(w) < 0.075f) spawn_at_random(w, kNear, uniform(w) < 0.5f ? 3 : 12);  // dat_82000D54
  if (uniform(w) < 0.1f) spawn_at_random(w, kFar, uniform(w) < 0.5f ? 1 : 22);     // dat_82000714
  if (t < 40.0f) {  // dat_8200105C
    if (progress <= 20.0f) return;  // dat_82000CE8
    const float u = uniform(w);
    if (u < 0.075f) {
      // State 1, corner streams.
      w.play(audio::Event::kBigWaveSoon);
      state = 1;
      if (uniform(w) < 0.5f) {
        group_type = 3;
        group_count = 5;
      } else {
        group_type = 2;
        group_count = 10;
      }
      return;
    }
    if (u < 0.15f) state = 4;  // dat_82000D58
    return;
  }
  if (uniform(w) < 0.1f) spawn_corners(w, uniform(w) < 0.8f ? 3 : 8);
  if (90.0f < progress) spawn_at_random(w, kFar, uniform(w) < 0.5f ? 2 : 3);  // dat_82001100
  // Repulsars at a corner by the C runtime's rand().
  if (75.0f < progress && uniform(w) < 0.075f) spawn_at_corner(w, 6, w.crt.next() % 4);   // dat_82000F74
  if (250.0f < progress && uniform(w) < 0.1f) spawn_at_corner(w, 6, w.crt.next() % 4);   // dat_82000D2C
  if (500.0f < progress && uniform(w) < 0.15f) spawn_at_corner(w, 6, w.crt.next() % 4);  // dat_82001170
  if (140.0f < progress) spawn_at_random(w, kFar, 2);  // dat_82000D28
  if (250.0f < progress) spawn_at_random(w, kFar, 3);
  if (325.0f < progress) spawn_at_random(w, kFar, 4);  // dat_8200116C
  if (400.0f < progress) spawn_at_random(w, kFar, 2);  // dat_82000F88
  if (500.0f < progress) spawn_at_random(w, kFar, 3);
  if (uniform(w) < 0.065f) {  // dat_82000D48
    // Twice, three snakes at a random corner.
    for (int i = 0; i < 2; ++i) {
      const int k = w.crt.next() % 4;
      for (int j = 0; j < 3; ++j) spawn_at_corner(w, 8, k);
    }
  }
  const float u = uniform(w);
  if (u < 0.03f) {  // dat_82001168
    // State 1, corner streams, more as the game goes on.
    timer = 1.0f;
    state = 1;
    const float u2 = uniform(w);
    if (u2 < 0.2f) {  // dat_82000D5C
      group_type = 3;
      group_count = 10;
    } else if (u2 < 0.4f) {  // dat_82001164
      group_type = 4;
      group_count = 8;
    } else if (u2 < 0.6f) {  // dat_82001160
      group_type = 8;
      group_count = 6;
    } else {
      group_type = 2;
      group_count = 20;
    }
    const float tt = progress;
    if (100.0f < tt) {
      float m;
      if (tt < 150.0f)  // dat_8200113C
        m = 1.25f;      // dat_82000768
      else if (tt < 250.0f)
        m = 1.5f;       // dat_82000C90
      else if (tt < 325.0f)
        m = 2.0f;       // dat_820006FC
      else if (tt < 425.0f)  // dat_8200115C
        m = 2.5f;       // dat_82001158
      else
        m = 4.0f;       // dat_820006F4
      group_count = int(float(group_count) * m);
    }
    w.play(audio::Event::kBigWaveSoon);
    return;
  }
  if (u < 0.06f) {  // dat_82001154
    // State 2, rings around the player.
    timer = 1.0f;
    state = 2;
    const float u2 = uniform(w);
    if (u2 < 0.2f) {
      group_type = 3;
      group_count = 7;
    } else if (u2 < 0.4f) {
      group_type = 4;
      group_count = 4;
    } else if (u2 < 0.6f) {
      group_type = 2;
      group_count = 12;
    } else {
      group_count = 8;
      group_type = u2 < 0.8f ? 12 : 8;
    }
    return;
  }
  if (u < 0.09f) {  // dat_82001150
    // State 3, a corner swarm, then gravity wells.
    state = 3;
    timer = 1.0f;
    group_count = 1;
    const float tt = progress;
    if (100.0f < tt) {
      if (tt < 200.0f)       // dat_82000CF0
        group_count = 2;
      else if (tt < 275.0f)  // dat_8200114C
        group_count = 3;
      else if (tt < 375.0f)  // dat_82001148
        group_count = 4;
      else
        group_count = 5;
    }
    return;
  }
  if (u < 0.12f) state = 4;  // dat_82001144
}

void WaveSpawner::evolved_corners(World& w) {
  timer = clamp_timer(0.6f, progress, 0.01f, 0.075f, 0.6f) * 1.25f;  // dat_82001160, dat_820011B0, dat_82000D54
  if (group_type == 4) timer = timer * 1.35f;                          // dat_8200119C
  group_count = group_count - 1;
  if (group_count < 1) {
    timer = timer * 10.0f;
    state = 0;
  }
  spawn_corners(w, group_type);
  if (300.0f < progress) spawn_at_random(w, kFar, 2);  // dat_82000CF4
  if (450.0f < progress) spawn_at_random(w, kFar, 3);  // dat_82001198
  if (500.0f < progress && uniform(w) < 0.5f) spawn_at_random(w, kFar, 4);
  if (600.0f < progress && uniform(w) < 0.3f) spawn_at_random(w, kFar, 6);  // dat_8200078C, dat_82000708
}

void WaveSpawner::evolved_rings(World& w) {
  const float m = group_type == 4 ? 1.35f : 1.0f;
  timer = clamp_timer(0.8f, progress, 0.02f, 0.3f, 0.8f);
  spawn_ring(w, m * 180.0f, group_type, group_count);       // dat_82000CD8
  spawn_ring(w, m * 230.0f, group_type, group_count << 1);  // dat_820011BC
  if (300.0f < progress) {
    spawn_at_random(w, kFar, 2);
    spawn_at_random(w, kFar, 2);
  }
  if (450.0f < progress) {
    spawn_at_random(w, kFar, 3);
    spawn_at_random(w, kFar, 3);
  }
  if (600.0f < progress) {
    spawn_at_random(w, kFar, 4);
    spawn_at_random(w, kFar, 4);
    spawn_at_random(w, kFar, 4);
  }
  if (350.0f < progress)  // dat_820011B8
    spawn_ring(w, m * 260.0f, group_type, int(float(group_count) * 2.5f));  // dat_820011B4
  timer = timer * 30.0f;
  state = 0;
}

void WaveSpawner::evolved_swarm(World& w) {
  timer = clamp_timer(1.2f, progress, 0.02f, 0.3f, 1.2f);  // dat_820011E0
  spawn_corners(w, 2);
  spawn_corners(w, 4);
  spawn_corners(w, 3);
  if (300.0f < progress) {
    spawn_at_random(w, kFar, 2);
    spawn_at_random(w, kFar, 2);
  }
  if (450.0f < progress) {
    spawn_at_random(w, kFar, 3);
    spawn_at_random(w, kFar, 3);
  }
  if (600.0f < progress) {
    spawn_at_random(w, kFar, 4);
    spawn_at_random(w, kFar, 4);
    spawn_at_random(w, kFar, 4);
  }
  if (700.0f < progress) {  // dat_820011D0
    spawn_at_random(w, kFar, 6);
    spawn_at_random(w, kFar, 6);
    spawn_at_random(w, kFar, 6);
  }
  group_count = group_count - 1;
  if (group_count < 1) {
    spawn_corners(w, 5);
    timer = timer * 30.0f;
    state = 0;
  }
}

void WaveSpawner::pacifism(World& w) {
  // A clump of grunts from a corner away from the player's quadrant
  // (any corner from wave 30), and a gate two waves in three.
  Vec2 pos{0.0f, 0.0f};
  if (30.0f <= progress) {  // dat_82000CDC (or multiplayer)
    pos = corner(int(w.rng.next() & 3));
  } else if (const PlayerTarget* p = first_player(w)) {
    for (int i = 0; i < 10; ++i) {
      const Vec2 c = corner(int(w.rng.next() & 3));
      pos = c;
      if ((0.0f < p->x) != (0.0f < c.x) || (0.0f < p->y) != (0.0f < c.y)) break;
    }
  }
  spawn_clump(w, 80.0f, 2, pos, 3 - int(progress * -0.3f));  // dat_82000D4C, dat_8200121C
  const int k = int(progress);
  if (k % 3 < 2) spawn_at_random(w, kNear, 16);
  const float t = progress;
  progress = t + 1.0f;
  timer = clamp_timer(3.0f, t, 0.5f, 1.0f, 3.5f);  // dat_82000C78, dat_82001218
}

void WaveSpawner::waves(World& w, bool refresh) {
  // Walls of rockets, full ones once geoms come faster than 20 a wave.
  const float ratio = float(int64_t(w.spawn_geoms)) / (progress + 1.0f);
  const int s = next_side(w, refresh);
  const bool big = !(ratio <= 20.0f);  // dat_82000CE8
  if (!big && !current_full)
    spawn_half_wall(w, 12, s, flip);
  else
    spawn_wall(w, 13, s, false);
  const float t = progress;
  if (!big) {
    if (5.0f < t) spawn_at_random(w, kFar, 22);    // dat_820006F0
    if (11.0f < t) spawn_at_random(w, kFar, 2);    // dat_820011F4
    if (17.0f < t) spawn_at_random(w, kFar, 3);    // dat_82000F90
    if (25.0f < t) spawn_at_random(w, kNear, 16);  // dat_82000D6C
    progress = t + 1.0f;
    timer = clamp_timer(4.25f, t, 0.125f, 0.25f, 4.25f);  // dat_820011F0, dat_82001204, dat_82000720
  } else {
    spawn_clump(w, 150.0f, 22, nearest_corner(w), 1);  // dat_8200113C
    if (4.0f < t) spawn_clump(w, 150.0f, 2, nearest_corner(w), 1);   // dat_820006F4
    if (8.0f < t) spawn_clump(w, 150.0f, 3, nearest_corner(w), 1);   // dat_82000D18
    if (12.0f < t) spawn_at_random(w, kNear, 16);                     // dat_82000D68
    progress = t + 1.0f;
    timer = clamp_timer(3.5625f, t, 0.125f, 0.25f, 3.5f);  // dat_82001200, dat_82001218
  }
  w.play(audio::Event::kSpawnRocketWave);
}

void WaveSpawner::king(World& w) {
  int n = 2 - int(progress * -0.2f);  // dat_82001208
  for (; n > 0; --n) {
    spawn_at_random(w, kFar, 1);
    spawn_at_random(w, kFar, 1);
    spawn_at_random(w, kFar, 2);
    spawn_at_random(w, kFar, 2);
    if (10.0f < progress) spawn_at_random(w, kFar, 8);
    if (20.0f < progress) spawn_at_random(w, kFar, 14);
  }
  const float t = progress;
  progress = t + 1.0f;
  timer = clamp_timer(3.0f, t, 0.5f, 1.0f, 3.5f) * 5.0f;  // dat_820006F0
}

}  // namespace geow2

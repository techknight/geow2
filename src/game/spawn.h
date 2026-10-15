#pragma once

#include "game/world.h"

namespace geow2 {

// A spawn position and facing.
struct Placement {
  float x = 0, y = 0, angle = 0;
};

// sub_8203BEE8(angle, type, &pos, flag): creates an enemy of a spawn type
// (1 wanderer, 2 grunt, 3 weaver, 4 spinner, 5 gravity well, 8 snake,
// 12/13 rocket, 16 gate, 22 duck, ...). Returns null for types not ported
// yet.
Entity* spawn_enemy(World& w, int type, float angle, float x, float y, bool flag = false);

// sub_8203BC08: a random arena position at least `dist` from every
// player's position a third of a second ahead (32 tries at most).
Vec2 spawn_position(World& w, float dist);

// sub_8203DC68: a group of `type` facing one of four right angles, at a
// spawn_position(dist).
void spawn_at_random(World& w, float dist, int type);

// One of four right angles from a random draw (sub_8203DC68, sub_8203D318).
float random_right_angle(World& w);

// sub_8203D550: on the arena edge nearest the player, in line with them,
// facing inward; with no player, the centre and a random right angle.
Placement placed_position(World& w);

}  // namespace geow2

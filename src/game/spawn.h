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

// sub_8204F3F8: the live player spawned earliest (with one player, that
// player), or null.
const PlayerTarget* first_player(const World& w);

// sub_8205B4C0: arena corner k (0 top left, 1 top right, 2 bottom right,
// 3 bottom left, at +-292, +-192); any other k, the centre.
Vec2 corner(int k);

// sub_8203B9A0: the corner nearest the first player; with no player, a
// random one.
Vec2 nearest_corner(World& w);

// sub_8203D840: one of `type` at each corner, facing 0.
void spawn_corners(World& w, int type);

// sub_8203D2C0: one of `type` at corner k, facing 0.
void spawn_at_corner(World& w, int type, int k);

// sub_8203D8B0: `n` of `type` evenly around the first player at `radius`,
// facing it (nothing with no player).
void spawn_ring(World& w, float radius, int type, int n);

// sub_8203D6A0: `n` of `type` scattered within `spread` of `centre`, each
// kept in the arena if four tries allow, facing a random right angle.
void spawn_clump(World& w, float spread, int type, Vec2 centre, int n);

// sub_8203C9C0: a line of `type` along a whole wall (side 0 top, 1
// bottom, 2 left, 3 right), 20 apart, facing into the arena.
void spawn_wall(World& w, int type, int side, bool flag);

// sub_8203CBE8: half such a line; `other` puts it on the half of the
// opposite wall.
void spawn_half_wall(World& w, int type, int side, bool other);

}  // namespace geow2

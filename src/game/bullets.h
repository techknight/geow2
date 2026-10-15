#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "game/game_math.h"
#include "game/math.h"
#include "game/player_motion.h"

namespace geow2 {

// A player bullet (constructor sub_82052DB8, update sub_82052418, wall
// handler sub_82051AF0). Types 0 (side) and 2 (centre) are the player's.
struct Bullet {
  float x = 0, y = 0, vx = 0, vy = 0, heading = 0;
  float radius = 2.0f;          // +0x20 (dat_820006FC)
  float sx = 0.9f, sy = 0.9f;  // +0x18 / +0x1C (0x3F666666 at 0x82000D70)
  float colour[4] = {1.0f, 0.75f, 0.1f, 1.0f};  // types 0 and 2 in single player
  float grid_radius = 6.0f;     // +0x8C (dat_82001414): its push on the grid
  float prev_x = 0, prev_y = 0;  // +0x90 / +0x94: position at the start of the step
  int type = 0;
  bool hit = false;             // +0x9C
  bool removed = false;         // flag 1: hit something, gone
};

// The line shape player bullets draw with (+0x40 / +0x3C when +0x7C != 1):
// six points (three segments) read from the user's image at 0x82181D4C.
struct BulletShape {
  std::vector<Vec2> points;
  static BulletShape from_image(std::span<const uint8_t> image);
};

struct PlayerWeapon {
  float cooldown = 0.0f;      // +0x80
  float upgrade_time = 0.0f;  // +0xD0 (seven bullets while > 0)
};

// The firing part of the player update (sub_8204FF00 → sub_8204EFD0):
// with `fire` the world fire-stick vector after the dead zone.
void player_fire_step(const PlayerMotion& p, PlayerWeapon& w, Vec2 fire, float step, const GameMath& gm,
                      std::vector<Bullet>& out);

// Per step for one bullet: returns false when it is removed this step (it
// hit a wall on the previous step). Otherwise moves it and handles the
// arena contact (sets `hit`).
bool bullet_step(Bullet& b, float step, float half_x, float half_y);

// World-space segments of a bullet (sub_82061908 with its shape).
struct Segment {
  Vec4 a, b;
};
std::vector<Segment> bullet_segments(const Bullet& b, const BulletShape& shape, const GameMath& gm);

// The shape transform of sub_82061908 / sub_82061A30: the table's points
// in pairs (one segment each), scaled, turned by `heading` and moved to
// (x, y), with the original's fused operations.
std::vector<Segment> shape_segments(std::span<const Vec2> points, float x, float y, float heading, float sx, float sy,
                                    const GameMath& gm);

// `count` points (x, y floats, big-endian) of a shape table in the user's
// image at `address`.
std::vector<Vec2> read_shape(std::span<const uint8_t> image, uint32_t address, int count);

}  // namespace geow2

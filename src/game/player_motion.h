#pragma once

#include <cstdint>

#include "game/game_math.h"

namespace geow2 {

struct Vec2 {
  float x = 0, y = 0;
};

// The parts of a player object that movement touches (offsets in the
// original object in comments).
struct PlayerMotion {
  float x = 0, y = 0;            // +0x04, +0x08
  float vx = 0, vy = 0;          // +0x0C, +0x10
  float heading = 0;             // +0x14
  float radius = 0;              // +0x20
  float thrust_level = 0;        // +0xB0
  float boost_time = 0;          // +0xCC: thrust 70 instead of 50 while > 0
  float slow_time = 0;           // +0xD8: thrust x 0.7 while > 0
  int32_t wall_sound_frames = 0;  // +0x94
};

// sub_82037EA0 axes 0..3: a raw XInput thumb value to [-1, 1]; Y axes are
// inverted (pushing up gives a negative value).
float stick_axis(int16_t raw, bool is_y);

// sub_8204EE28: dead zone (0.25) and magnitude clamp of a stick reading.
Vec2 stick_vector(float x, float y);

// The thrust, damping and heading part of the player update (sub_8204FF00).
// `move` is the stick vector in world space (the camera basis in normal
// play maps stick to world unchanged, see notes/re/input.md).
void player_thrust(PlayerMotion& p, Vec2 move, const GameMath& gm);

// sub_820453E0: position += velocity * step.
void integrate(PlayerMotion& p, float step);

// sub_8205C5D8: keeps a circle of `radius` inside the arena of half
// extents (half_x, half_y). Returns true on contact, with the clamped
// position written back and `normal` pointing into the arena.
bool arena_contact(float radius, float half_x, float half_y, float& x, float& y, Vec2& normal);

// sub_8204E908 (player vtable slot 5): slide along the wall. Returns true
// when the wall sound (event 7) should play.
bool player_wall(PlayerMotion& p, Vec2 normal);

}  // namespace geow2

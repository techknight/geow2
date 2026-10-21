#pragma once

#include "game/game_math.h"
#include "game/math.h"

namespace geow2 {

// sub_820318D8 with scale 1 and no translation: rotation from three angles
// in radians, rows as the original writes them (fused where it fuses).
Mat4 rotation_euler(const GameMath& gm, float a, float b, float c);

// sub_82041600: rotation of `angle` radians about the unit axis (x, y, z).
Mat4 rotation_axis(const GameMath& gm, float angle, float x, float y, float z);

// sub_82045CF0 for units of style None: rotate about z by the heading, then
// translate to (x, y, 0).
Mat4 placement_style_none(const GameMath& gm, float x, float y, float heading);

// sub_82047180: basis with row 2 = forward, row 1 = up made orthogonal to
// it, row 0 = their cross product, row 3 = position.
Mat4 look_at_basis(Vec4 position, Vec4 forward, Vec4 up);

// General 4x4 inverse (the original: sub_8206A1C8 / sub_82069FB8, in single-
// precision vector arithmetic with a hardware reciprocal estimate and two
// Newton-Raphson steps: results can differ in the last bits).
Mat4 inverse(const Mat4& m);

// sub_8206A300: left-handed perspective projection (row-vector convention).
Mat4 perspective(const GameMath& gm, float fov, float aspect, float near_z, float far_z);

}  // namespace geow2

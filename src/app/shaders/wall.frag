#version 450
#extension GL_GOOGLE_include_directive : require

// Pixel shader of preset 0x821B4688 (the walls): 13 reads of the light
// map's level-1 bloom (0x821B3794) in a cross, at the coordinate and
// 0.05, 0.15 and 0.25 away along u and v, summed; then max(sum - 0.05, 0)
// times 0.5, faded across the wall by sat(1 - 2 |side|) (literals c252 =
// (0, 1, -2, 0.5), c253..c255 the offsets).

#include "sampling.glsl"

layout(set = 2, binding = 0) uniform sampler2D light;
layout(location = 0) in vec3 v_uv_side;
layout(location = 0) out vec4 out_colour;

vec4 at(vec2 o) {
  return sample_linear_clamp(light, v_uv_side.xy + o);
}

void main() {
  const float fade = clamp(abs(v_uv_side.z) * -2.0 + 1.0, 0.0, 1.0);
  vec4 s = at(vec2(0.0)) + at(vec2(-0.05, 0.0));
  s = s + at(vec2(0.05, 0.0));
  s = s + at(vec2(0.0, -0.05));
  s = s + at(vec2(0.0, 0.05));
  s = s + at(vec2(-0.15, 0.0));
  s = s + at(vec2(0.15, 0.0));
  s = s + at(vec2(0.0, -0.15));
  s = s + at(vec2(0.0, 0.15));
  s = s + at(vec2(-0.25, 0.0));
  s = s + at(vec2(0.25, 0.0));
  s = s + at(vec2(0.0, -0.25));
  s = s + at(vec2(0.0, 0.25));
  s = s + -0.05;
  s = max(s, vec4(0.0));
  s = s * 0.5;
  out_colour = s * fade;
}

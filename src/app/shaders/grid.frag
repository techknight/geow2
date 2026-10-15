#version 450
#extension GL_GOOGLE_include_directive : require

// Pixel shader of preset 0x821B45C8 (the grid): the light map times the
// line's alpha level, squared and capped at 16, then faded across the
// line by sat(1 - |side|)^3 (literals 16 and 1; sides are +/- 1.125).

#include "sampling.glsl"

layout(set = 2, binding = 0) uniform sampler2D light_map;
layout(location = 0) in vec4 v_light;
layout(location = 0) out vec4 out_colour;

void main() {
  vec4 c = sample_linear_clamp(light_map, v_light.xy) * v_light.z;
  c = c * c;
  c = min(c, vec4(16.0));
  const float t = clamp(1.0 - abs(v_light.w), 0.0, 1.0);
  c = vec4(t) * c;
  c = c * t;
  out_colour = c * t;
}

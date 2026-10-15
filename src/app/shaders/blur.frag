#version 450
#extension GL_GOOGLE_include_directive : require

// Pixel shaders of presets 0x821B45F8 (horizontal) and 0x821B4604
// (vertical), the bloom blur of sub_82064930: 15 point reads along one
// axis, w0 for the centre and wk for the pair k texels away
// (w0..w3 = c0, w4..w7 = c1, from sub_82064848).

#include "sampling.glsl"

layout(set = 2, binding = 0) uniform sampler2D src;
layout(set = 3, binding = 0) uniform Pass {
  vec4 rect;
  vec4 axis;  // (1, 0) or (0, 1)
  vec4 c0;
  vec4 c1;
};
layout(location = 0) out vec4 out_colour;

vec4 pair(vec2 uv, float k) {
  return sample_point(src, uv, -axis.xy * k) + sample_point(src, uv, axis.xy * k);
}

void main() {
  const vec2 uv = pass_uv(rect);
  vec4 r = pair(uv, 7.0) * c1.w;
  r = pair(uv, 6.0) * c1.z + r;
  r = pair(uv, 5.0) * c1.y + r;
  r = pair(uv, 4.0) * c1.x + r;
  r = pair(uv, 3.0) * c0.w + r;
  r = pair(uv, 2.0) * c0.z + r;
  r = pair(uv, 1.0) * c0.y + r;
  out_colour = sample_point(src, uv, vec2(0.0)) * c0.x + r;
}

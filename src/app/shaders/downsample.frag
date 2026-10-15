#version 450
#extension GL_GOOGLE_include_directive : require

// Pixel shader of preset 0x821B461C (bloom downsample, sub_82064930):
// the mean of five bilinear reads, at the pixel and one texel away
// diagonally in each direction.

#include "sampling.glsl"

layout(set = 2, binding = 0) uniform sampler2D src;
layout(set = 3, binding = 0) uniform Pass {
  vec4 rect;
};
layout(location = 0) out vec4 out_colour;

void main() {
  const vec2 uv = pass_uv(rect);
  vec4 r = sample_linear(src, uv, vec2(-1.0, -1.0)) + sample_linear(src, uv, vec2(-1.0, 1.0));
  r = r + sample_linear(src, uv, vec2(1.0, -1.0));
  r = r + sample_linear(src, uv, vec2(1.0, 1.0));
  r = r + sample_linear(src, uv, vec2(0.0));
  out_colour = r * 0.2;
}

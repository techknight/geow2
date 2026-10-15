#version 450
#extension GL_GOOGLE_include_directive : require

// Pixel shader of preset 0x821B46AC (sub_82064F88): the five blurred
// bloom levels, read bilinearly and summed with weights c0.xyz, c1.xy.

#include "sampling.glsl"

layout(set = 2, binding = 0) uniform sampler2D level0;
layout(set = 2, binding = 1) uniform sampler2D level1;
layout(set = 2, binding = 2) uniform sampler2D level2;
layout(set = 2, binding = 3) uniform sampler2D level3;
layout(set = 2, binding = 4) uniform sampler2D level4;
layout(set = 3, binding = 0) uniform Pass {
  vec4 rect;
  vec4 c0;
  vec4 c1;
};
layout(location = 0) out vec4 out_colour;

void main() {
  const vec2 uv = pass_uv(rect);
  vec4 r = sample_linear(level4, uv, vec2(0.0)) * c1.y;
  r = sample_linear(level3, uv, vec2(0.0)) * c1.x + r;
  r = sample_linear(level2, uv, vec2(0.0)) * c0.z + r;
  r = sample_linear(level1, uv, vec2(0.0)) * c0.y + r;
  out_colour = sample_linear(level0, uv, vec2(0.0)) * c0.x + r;
}

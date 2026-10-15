#version 450
#extension GL_GOOGLE_include_directive : require

// Pixel shader of preset 0x821B4670: last frame's light map, read with
// clamp to edge.

#include "sampling.glsl"

layout(set = 2, binding = 0) uniform sampler2D previous;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_colour;

void main() {
  out_colour = sample_linear_clamp(previous, v_uv);
}

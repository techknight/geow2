#version 450
#extension GL_GOOGLE_include_directive : require

// Pixel shader of preset 0x821B4664 (sub_82065930): this frame's lights
// times MapLight.FeedbackMul plus last frame's warped map (point read)
// times MapLight.FeedbackAdd.

#include "sampling.glsl"

layout(set = 2, binding = 0) uniform sampler2D lights;
layout(set = 2, binding = 1) uniform sampler2D warped;
layout(set = 3, binding = 0) uniform Pass {
  vec4 rect;
  vec4 c0;  // (FeedbackMul, FeedbackAdd)
};
layout(location = 0) out vec4 out_colour;

void main() {
  const vec2 uv = pass_uv(rect);
  const vec4 r = sample_point(warped, uv, vec2(0.0)) * c0.y;
  out_colour = sample_linear(lights, uv, vec2(0.0)) * c0.x + r;
}

#version 450
#extension GL_GOOGLE_include_directive : require

// Pixel shader of preset 0x821B45EC, the final composite (sub_820653C8):
// background layer A and foreground layer B, each with its bloom. The
// foreground darkens the background behind it ("shadow"), and the result
// is raised to 1 / Render.Exposure.
//   c0 = (C, BloomBg * X * C), c1 = (Fc * C, BloomFg * X * Fc * C),
//   c2.x = 1 / Exposure, c3 = (ShadowFactor, ShadowAdd, ShadowFudge).
// The result is then stored as 8 bits and passed through the console's
// output gamma ramp (display_gamma.cpp), as the display hardware does.

#include "sampling.glsl"

layout(set = 2, binding = 0) uniform sampler2D layer_a;
layout(set = 2, binding = 1) uniform sampler2D bloom_a;
layout(set = 2, binding = 2) uniform sampler2D layer_b;
layout(set = 2, binding = 3) uniform sampler2D bloom_b;
layout(set = 3, binding = 0) uniform Pass {
  vec4 rect;
  vec4 c0;
  vec4 c1;
  vec4 c2;
  vec4 c3;
  vec4 ramp[64];  // 256 entries, four per vector
};
layout(location = 0) out vec4 out_colour;

void main() {
  const vec2 uv = pass_uv(rect);
  const vec3 a = sample_point(layer_a, uv, vec2(0.0)).rgb;
  const vec3 ab = sample_linear(bloom_a, uv, vec2(0.0)).rgb;
  const vec3 b = sample_point(layer_b, uv, vec2(0.0)).rgb;
  const vec3 bb = sample_linear(bloom_b, uv, vec2(0.0)).rgb;

  const vec3 back = a * c0.x + ab * c0.y;
  const vec3 fore = b * c1.x + bb * c1.y;
  const vec3 fore_sat = clamp(fore, 0.0, 1.0);
  const float peak = clamp(max(max(fore.r, fore.g), fore.b), 0.0, 1.0);
  // The shader's literal c255.x is 1.0.
  const float cover = clamp(c3.z * dot(fore_sat, vec3(1.0)), 0.0, 1.0);
  const vec3 plain = clamp(fore + back, 0.0, 1.0);
  const float inv = 1.0 - peak;
  const float shade = clamp(inv * c3.x * inv + c3.y, 0.0, 1.0);
  vec3 shaded = back * shade;
  shaded = shaded * shade;
  shaded = clamp(shaded * shade + fore, 0.0, 1.0);
  const vec3 mixed = cover * (shaded - plain) + plain;
  // exp2(log2(|v|) / Exposure); log2(0) is -infinity there, so 0 stays 0.
  const vec3 m = abs(mixed);
  const vec3 graded = exp2(log2(max(m, vec3(1e-30))) * c2.x);
  const vec3 rgb = mix(vec3(0.0), graded, greaterThan(m, vec3(0.0)));
  const ivec3 k = ivec3(round(clamp(rgb, 0.0, 1.0) * 255.0));
  const vec3 shown = vec3(ramp[k.r >> 2][k.r & 3], ramp[k.g >> 2][k.g & 3], ramp[k.b >> 2][k.b & 3]);
  // The original writes alpha 0; the port writes 1 so screenshots are
  // opaque (the window ignores alpha).
  out_colour = vec4(shown, 1.0);
}

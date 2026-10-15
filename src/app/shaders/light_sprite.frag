#version 450

// Pixel shader of preset 0x821B46C4: the sprite texture (8-bit luminance,
// read as (L, L, L, 1)) times the colour, drawn additively.

layout(set = 2, binding = 0) uniform sampler2D sprite;
layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_colour;
layout(location = 0) out vec4 out_colour;

void main() {
  const float l = texture(sprite, v_uv).r;
  out_colour = vec4(l, l, l, 1.0) * v_colour;
}

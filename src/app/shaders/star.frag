#version 450

// Pixel shader of preset 0x821B4658: the sprite texture (3 x 3, white in
// the middle texel only, `dat_821B4768`) at the sprite coordinate, times
// the colour. Drawn additively.

layout(set = 2, binding = 0) uniform sampler2D sprite;
layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_colour;
layout(location = 0) out vec4 out_colour;

void main() {
  out_colour = texture(sprite, abs(v_uv)) * v_colour;
}

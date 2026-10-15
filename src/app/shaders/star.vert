#version 450

// Stars (sub_820756F8, preset 0x821B4658): point sprites, 3 pixels of the
// original's back buffer wide (sub_8209BD48(3.0)), centred on the star's
// position times worldViewProjection, with sprite coordinates 0..1 across
// and the star's colour. One instance per star, six vertices per sprite.

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_colour;
layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_colour;

layout(set = 1, binding = 0) uniform Transform {
  mat4 view_projection;
  vec4 sprite_size;  // the sprite's width and height in clip space units (2 * pixels / viewport)
};

const vec2 kCorner[6] = vec2[](vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 0), vec2(1, 1), vec2(0, 1));

void main() {
  const vec4 centre = view_projection * vec4(in_position, 1.0);
  const vec2 c = kCorner[gl_VertexIndex];
  // Sprite coordinates start at the top left; clip space y points up.
  gl_Position = centre + vec4((c.x - 0.5) * sprite_size.x, (0.5 - c.y) * sprite_size.y, 0.0, 0.0) * centre.w;
  v_uv = c;
  v_colour = vec4(in_colour, 1.0);
}

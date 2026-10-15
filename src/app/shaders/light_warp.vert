#version 450

// Vertex shader of preset 0x821B4670 (light map warp, sub_82065930): each
// grid point is placed at its rest position in the map, (2u - 1, 1 - 2v),
// and reads last frame's map displaced by its offset from rest:
// (u - 0.00025 dx, v + 0.00025 dy) (literals c254, c255).

layout(location = 0) in vec2 in_offset;  // position - rest
layout(location = 1) in vec2 in_uv;      // the point's texture-like coordinates
layout(location = 0) out vec2 v_uv;

void main() {
  gl_Position = vec4(in_uv.x * 2.0 - 1.0, in_uv.y * -2.0 + 1.0, 0.0, 1.0);
  v_uv = vec2(in_offset.x * -0.00025 + in_uv.x, in_offset.y * 0.00025 + in_uv.y);
}

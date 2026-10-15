#version 450

// Vertex shader of the line presets 0x821B4694 (body) and 0x821B46B8
// (caps), assembled in sub_8206AD10: the position (x, y, z, 1) times the
// four worldViewProjection rows (uploaded by sub_82067A20); the colour
// and the side marker pass through. The row-major matrix is uploaded as
// is, which GLSL reads as its transpose, so `m * v` is the original's
// row-vector product v * M.

layout(location = 0) in vec4 in_position;  // x, y, z, side
layout(location = 1) in vec4 in_colour;
layout(location = 0) out vec4 v_colour;
layout(location = 1) out float v_side;

layout(set = 1, binding = 0) uniform Transform {
  mat4 view_projection;
};

void main() {
  gl_Position = view_projection * vec4(in_position.xyz, 1.0);
  v_colour = in_colour;
  v_side = in_position.w;
}

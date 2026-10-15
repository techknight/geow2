#version 450

// Provisional line shader: positions arrive already in world space (the
// original transforms line endpoints on the CPU, sub_82061DA0); the
// row-major view-projection is uploaded as is, which GLSL reads as its
// transpose, so `m * v` is the original's row-vector product v * M.

layout(location = 0) in vec4 in_position;
layout(location = 1) in vec4 in_colour;
layout(location = 0) out vec4 v_colour;

layout(set = 1, binding = 0) uniform Transform {
  mat4 view_projection;
};

void main() {
  gl_Position = view_projection * vec4(in_position.xyz, 1.0);
  v_colour = in_colour;
}

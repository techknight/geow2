#version 450

// Vertex shader of preset 0x821B4688 (the walls, sub_8205CF20): position
// (x, y, z, 1) times worldViewProjection; the light-map coordinate and the
// side marker pass through.

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in float in_side;
layout(location = 0) out vec3 v_uv_side;

layout(set = 1, binding = 0) uniform Transform {
  mat4 view_projection;
};

void main() {
  gl_Position = view_projection * vec4(in_position, 1.0);
  v_uv_side = vec3(in_uv, in_side);
}

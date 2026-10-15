#version 450

// Vertex shader of preset 0x821B45C8 (the grid, sub_8205CAF0): position
// (x, y, z, 1) times worldViewProjection; the light map is read at the
// point's arena position, (x / 600 + 0.5, -y / 400 + 0.5) (the shader's
// literals); the alpha level and side marker pass through.

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_alpha_side;
layout(location = 0) out vec4 v_light;  // light-map u, v, alpha, side

layout(set = 1, binding = 0) uniform Transform {
  mat4 view_projection;
};

void main() {
  gl_Position = view_projection * vec4(in_position, 1.0);
  const vec2 scale = vec2(uintBitsToFloat(0x3ADA740Eu), uintBitsToFloat(0xBB23D70Au));
  v_light = vec4(in_position.xy * scale + 0.5, in_alpha_side);
}

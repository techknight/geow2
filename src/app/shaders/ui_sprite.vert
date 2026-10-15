#version 450

// Vertex shader of the bent sprite preset 0x821B46D0 (Xenia's dump
// 0EBD9D4A6860B61C): the UI cylinder bend of ui_line.vert with z = 0,
// passing the texture coordinates and the colour on.

layout(location = 0) in vec4 in_position;  // x, y, u, v
layout(location = 1) in vec4 in_colour;
layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_colour;

layout(set = 1, binding = 0) uniform Transform {
  mat4 ui_transform;
};

void main() {
  const float pi = 3.14159265;
  const vec3 r = vec3(in_position.x - 320.0, in_position.y - 180.0, 320.0);
  const vec2 theta = fract(r.xy * (1.0 / (1500.0 * pi)) + 0.5) * (2.0 * pi) - pi;
  const vec4 bent = vec4(750.0 * sin(theta.x) + 320.0, 750.0 * sin(theta.y) + 180.0, r.z - 750.0 * cos(theta.x), 1.0);
  gl_Position = ui_transform * bent;
  v_uv = in_position.zw;
  v_colour = in_colour;
}

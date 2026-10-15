#version 450

// Vertex shader of the UI line presets 0x821B45B0 (body) and 0x821B45BC
// (caps) (Xenia's dump 805D1C6EAF91C26B, literals from sub_8206AD10): the
// UI (640 x 360 units) is bent onto a cylinder of radius 750 before the UI
// transform (sub_82067E40): r = (x - 320, y - 180, z + 320); theta =
// frac(r * 1/(1500 pi) + 0.5) * 2 pi - pi per axis; position (750 sin
// theta.x + 320, 750 sin theta.y + 180, r.z - 750 cos theta.x, 1).

layout(location = 0) in vec4 in_position;  // x, y, z, side
layout(location = 1) in vec4 in_colour;
layout(location = 0) out vec4 v_colour;
layout(location = 1) out float v_side;

layout(set = 1, binding = 0) uniform Transform {
  mat4 ui_transform;
};

void main() {
  const float pi = 3.14159265;
  const vec3 r = vec3(in_position.x - 320.0, in_position.y - 180.0, in_position.z + 320.0);
  const vec2 theta = fract(r.xy * (1.0 / (1500.0 * pi)) + 0.5) * (2.0 * pi) - pi;
  const vec4 bent = vec4(750.0 * sin(theta.x) + 320.0, 750.0 * sin(theta.y) + 180.0, r.z - 750.0 * cos(theta.x), 1.0);
  gl_Position = ui_transform * bent;
  v_colour = in_colour;
  v_side = in_position.w;
}

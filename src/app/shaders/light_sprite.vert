#version 450

// Vertex shader of preset 0x821B46C4 (light sprites): position (x, y, 0, 1)
// times the light-map camera (sub_820682B8: x / 300, y / 200), the
// texture coordinate and the colour (r, g, b, 1).

layout(location = 0) in vec2 in_position;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec3 in_colour;
layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_colour;

void main() {
  // dat_82002DC8 = 1/300, dat_82001B54 = 1/200 (as floats).
  const vec2 scale = vec2(uintBitsToFloat(0x3B5A740Eu), uintBitsToFloat(0x3BA3D70Au));
  gl_Position = vec4(in_position * scale, 0.0, 1.0);
  v_uv = in_uv;
  v_colour = vec4(in_colour, 1.0);
}

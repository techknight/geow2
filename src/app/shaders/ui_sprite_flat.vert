#version 450

// Vertex shader of the flat sprite preset 0x821B46DC (Xenia's dump
// 7D9796B07863B4EB) after sub_820685B0: no viewport transform, positions
// scaled from UI units (640 x 360) to the back buffer's pixels, y down.

layout(location = 0) in vec4 in_position;  // x, y, u, v
layout(location = 1) in vec4 in_colour;
layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_colour;

void main() {
  gl_Position = vec4(in_position.x / 320.0 - 1.0, 1.0 - in_position.y / 180.0, 0.0, 1.0);
  v_uv = in_position.zw;
  v_colour = in_colour;
}

#version 450

// Pixel shader of preset 0x821B4694 (line bodies): t = 1 - |side|, the
// colour times t four times over, alpha included.

layout(location = 0) in vec4 v_colour;
layout(location = 1) in float v_side;
layout(location = 0) out vec4 out_colour;

void main() {
  const float t = 1.0 - abs(v_side);
  vec4 c = vec4(t) * v_colour;
  c = c * t;
  c = c * t;
  out_colour = c * t;
}

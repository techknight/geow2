#version 450

// Pixel shader of preset 0x821B4610 (particle streaks, Render.Layer.Particle):
// the colour times 4 (2 - a^2), a running -1 to +1 along the streak, times
// (1 - |side|)^4 across it (literals 1, 2, 4).

layout(location = 0) in vec4 v_colour;
layout(location = 1) in float v_side;
layout(location = 0) out vec4 out_colour;

void main() {
  const float t = 1.0 - abs(v_side);
  const float red = 4.0 * v_colour.r;
  const float along = -v_colour.a * v_colour.a + 2.0;
  const float k = 4.0 * along;
  vec4 c = vec4(red * along, k * v_colour.g, k * v_colour.b, k * v_colour.a);
  c = c * t;
  c = c * t;
  c = c * t;
  out_colour = c * t;
}

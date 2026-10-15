#version 450

// Pixel shader of preset 0x821B46B8 (line end caps): t = 1 - |side|; the
// colour times t^4 and times a^6, a being the cap's alpha slot (1 at the
// line end, 0 at the tip); alpha written as 1.

layout(location = 0) in vec4 v_colour;
layout(location = 1) in float v_side;
layout(location = 0) out vec4 out_colour;

void main() {
  float t = 1.0 - abs(v_side);
  t = t * t;
  t = t * t;
  const float a2 = v_colour.a * v_colour.a;
  const vec3 rgb = vec3(t) * v_colour.rgb;
  float a = a2 * v_colour.a;
  a = a * a;
  out_colour = vec4(rgb * a, 1.0);
}

#version 450

// Pixel shader of the panel fill preset 0x821B467C (Xenia's dump
// 02758B8CCB028F46): the constant c255.xxxy = (0.125, 0.125, 0.125, 0.5)
// whatever the vertex colour. Drawn with the blend 0x1000741 (colour op
// MIN, alpha kept): a panel caps what is behind it at 0.125.

layout(location = 0) in vec4 v_colour;
layout(location = 1) in float v_side;
layout(location = 0) out vec4 out_colour;

void main() { out_colour = vec4(0.125, 0.125, 0.125, 0.5); }

#version 450

// Pixel shader of both sprite presets (Xenia's dump 0635A0D64188B1C0):
// T = tex2D(uv); out = (T.r * C.r * T.r, T.g * C.g * T.g, T.b * C.b *
// T.b, T.a * T.a), the texture squared (a gamma-2 decode) times the
// queue's colour. The sampler filters linearly and clamps to a black
// border (fetch constant clamp mode 6); the border is reproduced here with
// a hand-made bilinear fetch.

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_colour;
layout(location = 0) out vec4 out_colour;

layout(set = 2, binding = 0) uniform sampler2D tex;

vec4 texel(ivec2 p, ivec2 size) {
  if (p.x < 0 || p.y < 0 || p.x >= size.x || p.y >= size.y) return vec4(0.0);
  return texelFetch(tex, p, 0);
}

void main() {
  const ivec2 size = textureSize(tex, 0);
  const vec2 c = v_uv * vec2(size) - 0.5;
  const ivec2 i = ivec2(floor(c));
  const vec2 f = c - vec2(i);
  const vec4 t = mix(mix(texel(i, size), texel(i + ivec2(1, 0), size), f.x),
                     mix(texel(i + ivec2(0, 1), size), texel(i + ivec2(1, 1), size), f.x), f.y);
  out_colour = vec4((t.r * v_colour.r) * t.r, (t.g * v_colour.g) * t.g, (t.b * v_colour.b) * t.b, t.a * t.a);
}

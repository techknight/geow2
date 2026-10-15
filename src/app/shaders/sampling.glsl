// Texture reads of the full-screen passes, as the original's samplers do
// them (sub_82068718, sub_820653C8): address mode "clamp to border" with
// a black border, point or bilinear filtering, and offsets in texels of
// the texture read.
//
// Every texture of the chain holds what a 7e3 render target can (resolved
// with exponent bias -5 into a fixed-point texture fetched with +5,
// sub_820653C8 / sub_8206A3D0), so values read are capped at 31.875.

const float kTargetMax = 31.875;

// The pass's target rectangle (x, y, width, height) in pixels.
vec2 pass_uv(vec4 rect) {
  return (gl_FragCoord.xy - rect.xy) / rect.zw;
}

vec4 texel(sampler2D t, ivec2 i) {
  const ivec2 size = textureSize(t, 0);
  if (any(lessThan(i, ivec2(0))) || any(greaterThanEqual(i, size))) return vec4(0.0);
  return min(texelFetch(t, i, 0), vec4(kTargetMax));
}

vec4 sample_point(sampler2D t, vec2 uv, vec2 offset) {
  const vec2 c = uv * vec2(textureSize(t, 0)) + offset;
  return texel(t, ivec2(floor(c)));
}

vec4 sample_linear(sampler2D t, vec2 uv, vec2 offset) {
  const vec2 c = uv * vec2(textureSize(t, 0)) - 0.5 + offset;
  const vec2 i = floor(c);
  const vec2 f = c - i;
  const ivec2 i0 = ivec2(i);
  const vec4 top = mix(texel(t, i0), texel(t, i0 + ivec2(1, 0)), f.x);
  const vec4 bottom = mix(texel(t, i0 + ivec2(0, 1)), texel(t, i0 + ivec2(1, 1)), f.x);
  return mix(top, bottom, f.y);
}

// Bilinear read with the address mode "clamp to edge" (mode 2), as the
// light map is read by the grid and by its own warp pass.
vec4 sample_linear_clamp(sampler2D t, vec2 uv) {
  const ivec2 size = textureSize(t, 0);
  const vec2 c = uv * vec2(size) - 0.5;
  const vec2 i = floor(c);
  const vec2 f = c - i;
  const ivec2 i0 = clamp(ivec2(i), ivec2(0), size - 1);
  const ivec2 i1 = clamp(ivec2(i) + 1, ivec2(0), size - 1);
  const vec4 top = mix(texel(t, i0), texel(t, ivec2(i1.x, i0.y)), f.x);
  const vec4 bottom = mix(texel(t, ivec2(i0.x, i1.y)), texel(t, i1), f.x);
  return mix(top, bottom, f.y);
}

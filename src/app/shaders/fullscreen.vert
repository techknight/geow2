#version 450

// Full-screen passes. The original draws a screen-space rectangle with
// texture coordinates 0..1 over the target (sub_82068718) and pixel
// centres at +0.5 (sub_8209C570); the fragment shaders here derive the
// same coordinates from gl_FragCoord, so one covering triangle does.

void main() {
  const vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}

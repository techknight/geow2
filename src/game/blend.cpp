#include "game/blend.h"

#include <cmath>

namespace geow2 {
namespace {

float lerp(float a, float b, float s) { return a + (b - a) * s; }

// Range i's ends in a batch: [body, body_end) and [caps, caps_end).
struct Span {
  size_t body, body_end, caps, caps_end;
};

Span span_of(const LineBatch& b, size_t i) {
  const LineRange& r = b.ranges[i];
  const bool last = i + 1 == b.ranges.size();
  return {r.body, last ? b.body.size() : b.ranges[i + 1].body, r.caps, last ? b.caps.size() : b.ranges[i + 1].caps};
}

void blend_vertices(const std::vector<LineVertex>& prev, size_t from, size_t to, size_t n, float s,
                    std::vector<LineVertex>& out) {
  for (size_t k = 0; k < n; ++k) {
    const LineVertex& p = prev[from + k];
    LineVertex& o = out[to + k];
    o.x = lerp(p.x, o.x, s);
    o.y = lerp(p.y, o.y, s);
    o.z = lerp(p.z, o.z, s);
  }
}

}  // namespace

void LineBlend::blend(const LineBatch& prev, const LineBatch& cur, float s, LineBatch& out) {
  out.body = cur.body;
  out.caps = cur.caps;
  out.ranges = cur.ranges;
  prev_ranges_.clear();
  for (size_t i = 0; i < prev.ranges.size(); ++i)
    if (prev.ranges[i].key != kNoBlend) prev_ranges_[prev.ranges[i].key] = i;
  for (size_t i = 0; i < cur.ranges.size(); ++i) {
    if (cur.ranges[i].key == kNoBlend) continue;
    const auto it = prev_ranges_.find(cur.ranges[i].key);
    if (it == prev_ranges_.end()) continue;  // new this step
    const Span a = span_of(prev, it->second), b = span_of(cur, i);
    const size_t body = b.body_end - b.body, caps = b.caps_end - b.caps;
    if (a.body_end - a.body != body || a.caps_end - a.caps != caps) continue;  // its lines changed
    blend_vertices(prev.body, a.body, b.body, body, s, out.body);
    blend_vertices(prev.caps, a.caps, b.caps, caps, s, out.caps);
  }
}

void blend_grid(const std::vector<GridVertex>& prev, const std::vector<GridVertex>& cur, float s,
                std::vector<GridVertex>& out) {
  out = cur;
  if (prev.size() != cur.size()) return;
  for (size_t k = 0; k < out.size(); ++k) {
    out[k].x = lerp(prev[k].x, cur[k].x, s);
    out[k].y = lerp(prev[k].y, cur[k].y, s);
    out[k].z = lerp(prev[k].z, cur[k].z, s);
  }
}

void shift_streaks(const std::vector<LineVertex>& cur, float line_height, float s, std::vector<LineVertex>& out) {
  out = cur;
  const float back = 1.0f - s;
  for (size_t q = 0; q + 3 < out.size(); q += 4) {
    // From vertex 0 to vertex 3 is the move plus twice the line height
    // along it.
    const LineVertex& v0 = cur[q];
    const LineVertex& v3 = cur[q + 3];
    const float ex = v3.x - v0.x, ey = v3.y - v0.y;
    const float len = std::sqrt(ex * ex + ey * ey);
    const float k = len > 2.0f * line_height ? (len - 2.0f * line_height) / len : 0.0f;
    const float dx = ex * k * back, dy = ey * k * back, dz = (v3.z - v0.z) * back;
    for (size_t i = q; i < q + 4; ++i) {
      out[i].x -= dx;
      out[i].y -= dy;
      out[i].z -= dz;
    }
  }
}

CameraState blend_camera(const CameraState& prev, const CameraState& cur, float s) {
  return {lerp(prev.x, cur.x, s), lerp(prev.y, cur.y, s), lerp(prev.zoom, cur.zoom, s)};
}

}  // namespace geow2

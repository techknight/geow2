#include "game/popups.h"

namespace geow2 {
namespace {

// Line half-width factor while the linger layer draws (dat_821825F4 =
// dat_82002A90).
constexpr float kLingerLineFactor = 0.667f;

// The five colours by index, from a fade f (and f squared).
void popup_colour(int index, double f, float out[4], float alpha0) {
  const float f2 = float(f * f);
  const float a = float(f * 2.0);  // dat_820006FC
  switch (index) {
    case 0:
      out[0] = a;
      out[1] = float(double(f2) * 1.5);  // dat_82000C90
      out[2] = 0.0f;
      out[3] = alpha0;
      break;
    case 1:
      out[0] = a;
      out[1] = float(double(f2) * 0.5);   // dat_82000724
      out[2] = float(double(f2) * 0.25);  // dat_82000720
      out[3] = 1.0f;
      break;
    case 2:
      out[0] = float(double(f2) * 0.25);
      out[1] = float(double(f2) * 1.5);
      out[2] = a;
      out[3] = 1.0f;
      break;
    case 3:
      out[0] = a;
      out[1] = float(double(f2) * 1.5);
      out[2] = float(double(f2) * 0.25);
      out[3] = 1.0f;
      break;
    default:
      out[0] = float(double(f2) * 0.75);  // dat_82000718
      out[1] = a;
      out[2] = float(double(f2) * 0.5);
      out[3] = 1.0f;
      break;
  }
}

}  // namespace

void Popups::add_small(float x, float y, float size, int colour, const std::string& text) {
  if (budget_ <= 0) return;
  Small& s = small_[size_t(write_)];
  s.text = text.substr(0, 15);
  s.x = x;
  s.y = y;
  s.colour = colour;
  s.size = size;
  ++pending_;
  --budget_;
  write_ = (write_ + 1) % 64;
}

void Popups::add_big(float x, float y, float duration, int colour, const std::string& text) {
  for (Big& b : big_) {
    if (!(b.duration <= b.time)) continue;
    b.text = text.substr(0, 31);
    b.x = x;
    b.y = y;
    b.colour = colour;
    b.duration = duration;
    b.time = 0.0f;
    return;
  }
}

void Popups::step(float dt) {
  frames_[size_t(frame_)] = pending_;
  frame_ = (frame_ + 1) & 31;
  pending_ = 0;
  budget_ = frames_[size_t(frame_)] + budget_;
  first_ = (frames_[size_t(frame_)] + first_) & 63;
  for (Big& b : big_)
    if (b.time < b.duration) b.time = b.time + dt;
}

void Popups::draw(const Font& font, LineBatch& out) const {
  font.set_line_factor(kLingerLineFactor);
  // Small ones, oldest frame first: f = 1 - (age / 32)^2 sizes and fades
  // them; they rise by half their size.
  int slot = frame_, entry = first_;
  for (int age = 31; age > 0; --age) {
    slot = (slot + 1) & 31;
    const int n = frames_[size_t(slot)];
    if (n <= 0) continue;
    const float t = float(age) * 0.03125f;  // dat_82001614
    const double f = -(double(t * t) - 1.0);
    const float h = float(f * 8.0);  // dat_82000D18
    const float rise = float(double(h) * 0.5);
    for (int k = 0; k < n; ++k) {
      const Small& s = small_[size_t(entry)];
      float c[4];
      popup_colour(s.colour, f, c, 0.85f);  // dat_82000D1C
      const float size = float(double(s.size) * double(h));
      font.centre(out, s.x, float(double(s.y) - double(rise)), size, size, c, s.text);
      entry = (entry + 1) & 63;
    }
  }
  // Big ones: full size until their last second.
  for (const Big& b : big_) {
    if (!(b.time < b.duration)) continue;
    double d = double(float(double(b.time - b.duration) + 1.0));
    if (d < 0.0) d = 0.0;
    const double f = -(double(float(d * d)) - 1.0);
    float c[4];
    if (b.colour == 4) {
      // Index 4 here: (0.4 f^2, 2 f, 0.2 f^2) (dat_82001164, dat_82000D5C).
      const float f2 = float(f * f);
      c[0] = float(double(f2) * 0.4);
      c[1] = float(f * 2.0);
      c[2] = float(double(f2) * 0.2);
      c[3] = 1.0f;
    } else {
      popup_colour(b.colour, f, c, 0.85f);
    }
    const float size = float(f * 8.0);
    font.centre(out, b.x, float(double(b.y) - double(size) * 0.5), size, size, c, b.text);
  }
  font.set_line_factor(1.0f);
}

}  // namespace geow2

#include "data/tga.h"

namespace geow2 {

std::optional<Image> decode_tga(std::span<const uint8_t> f) {
  if (f.size() < 18) return std::nullopt;
  const uint32_t id_length = f[0], colour_map = f[1], type = f[2];
  const uint32_t w = uint32_t(f[12]) | uint32_t(f[13]) << 8, h = uint32_t(f[14]) | uint32_t(f[15]) << 8;
  const uint32_t bpp = f[16], descriptor = f[17];
  if (colour_map != 0 || (type != 2 && type != 10) || (bpp != 24 && bpp != 32) || w == 0 || h == 0) return std::nullopt;
  const size_t bytes = bpp / 8;
  size_t p = 18 + id_length;
  // The pixels in file order.
  std::vector<uint8_t> px(size_t(w) * h * 4);
  auto put = [&](size_t i, const uint8_t* s) {
    px[i * 4 + 0] = s[2];
    px[i * 4 + 1] = s[1];
    px[i * 4 + 2] = s[0];
    px[i * 4 + 3] = bytes == 4 ? s[3] : 0xFF;
  };
  const size_t n = size_t(w) * h;
  if (type == 2) {
    if (p + n * bytes > f.size()) return std::nullopt;
    for (size_t i = 0; i < n; ++i) put(i, &f[p + i * bytes]);
  } else {
    size_t i = 0;
    while (i < n) {
      if (p >= f.size()) return std::nullopt;
      const uint8_t head = f[p++];
      const size_t count = (head & 0x7F) + 1;
      if (head & 0x80) {
        if (p + bytes > f.size()) return std::nullopt;
        for (size_t k = 0; k < count && i < n; ++k) put(i++, &f[p]);
        p += bytes;
      } else {
        if (p + count * bytes > f.size()) return std::nullopt;
        for (size_t k = 0; k < count && i < n; ++k, p += bytes) put(i++, &f[p]);
      }
    }
  }
  Image img;
  img.width = w;
  img.height = h;
  img.rgba.resize(px.size());
  const bool top_down = descriptor & 0x20, right_to_left = descriptor & 0x10;
  for (uint32_t row = 0; row < h; ++row) {
    const uint32_t y = top_down ? row : h - 1 - row;
    for (uint32_t col = 0; col < w; ++col) {
      const uint32_t x = right_to_left ? w - 1 - col : col;
      const size_t src = (size_t(row) * w + col) * 4, dst = (size_t(y) * w + x) * 4;
      for (int c = 0; c < 4; ++c) img.rgba[dst + size_t(c)] = px[src + size_t(c)];
    }
  }
  return img;
}

}  // namespace geow2

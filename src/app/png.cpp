#include "app/png.h"

#include <array>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace geow2 {
namespace {

uint32_t crc32(const uint8_t* data, size_t len, uint32_t crc = 0) {
  static const std::array<uint32_t, 256> table = [] {
    std::array<uint32_t, 256> t{};
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i;
      for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      t[i] = c;
    }
    return t;
  }();
  crc = ~crc;
  for (size_t i = 0; i < len; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
  return ~crc;
}

void put32(std::vector<uint8_t>& out, uint32_t v) {
  for (int i = 3; i >= 0; --i) out.push_back(uint8_t(v >> (i * 8)));
}

void chunk(std::vector<uint8_t>& out, const char type[4], const std::vector<uint8_t>& body) {
  put32(out, uint32_t(body.size()));
  std::vector<uint8_t> tb(type, type + 4);
  tb.insert(tb.end(), body.begin(), body.end());
  out.insert(out.end(), tb.begin(), tb.end());
  put32(out, crc32(tb.data(), tb.size()));
}

}  // namespace

void write_png(const std::filesystem::path& path, uint32_t width, uint32_t height, std::span<const uint8_t> rgba) {
  if (rgba.size() < size_t(width) * height * 4) throw std::runtime_error("png: not enough pixels");
  // Raw scanlines with filter byte 0.
  std::vector<uint8_t> raw;
  raw.reserve((size_t(width) * 4 + 1) * height);
  for (uint32_t y = 0; y < height; ++y) {
    raw.push_back(0);
    const uint8_t* row = &rgba[size_t(y) * width * 4];
    raw.insert(raw.end(), row, row + size_t(width) * 4);
  }
  // zlib stream of stored blocks (at most 65535 bytes each) and Adler-32.
  std::vector<uint8_t> z{0x78, 0x01};
  for (size_t pos = 0; pos < raw.size() || raw.empty();) {
    const size_t n = std::min<size_t>(65535, raw.size() - pos);
    const bool last = pos + n == raw.size();
    z.push_back(last ? 1 : 0);
    z.push_back(uint8_t(n));
    z.push_back(uint8_t(n >> 8));
    z.push_back(uint8_t(~n));
    z.push_back(uint8_t(~n >> 8));
    z.insert(z.end(), raw.begin() + std::ptrdiff_t(pos), raw.begin() + std::ptrdiff_t(pos + n));
    pos += n;
    if (last) break;
  }
  uint32_t a = 1, b = 0;
  for (uint8_t c : raw) {
    a = (a + c) % 65521;
    b = (b + a) % 65521;
  }
  put32(z, (b << 16) | a);

  std::vector<uint8_t> out{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  std::vector<uint8_t> ihdr;
  put32(ihdr, width);
  put32(ihdr, height);
  ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});  // 8-bit RGBA, no interlace
  chunk(out, "IHDR", ihdr);
  chunk(out, "IDAT", z);
  chunk(out, "IEND", {});
  std::ofstream f(path, std::ios::binary);
  if (!f) throw std::runtime_error("png: cannot write " + path.string());
  f.write(reinterpret_cast<const char*>(out.data()), std::streamsize(out.size()));
}

}  // namespace geow2

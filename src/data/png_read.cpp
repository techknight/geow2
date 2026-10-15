#include "data/png_read.h"

#include <array>
#include <cstdlib>
#include <cstring>

namespace geow2 {
namespace {

struct BitReader {
  std::span<const uint8_t> d;
  size_t pos = 0;
  uint32_t bits = 0;
  int count = 0;
  bool bad = false;
  uint32_t get(int n) {
    while (count < n) {
      if (pos >= d.size()) {
        bad = true;
        return 0;
      }
      bits |= uint32_t(d[pos++]) << count;
      count += 8;
    }
    const uint32_t v = bits & ((1u << n) - 1);
    bits >>= n;
    count -= n;
    return v;
  }
};

// A canonical Huffman code (counts per length, symbols by code).
struct Huffman {
  std::array<uint16_t, 16> counts{};
  std::array<uint16_t, 320> symbols{};
  bool build(const uint8_t* lengths, int n) {
    counts.fill(0);
    for (int i = 0; i < n; ++i) ++counts[lengths[i]];
    counts[0] = 0;
    std::array<uint16_t, 16> offs{};
    for (int i = 1; i < 16; ++i) offs[size_t(i)] = uint16_t(offs[size_t(i - 1)] + counts[size_t(i - 1)]);
    for (int i = 0; i < n; ++i)
      if (lengths[i]) symbols[offs[lengths[i]]++] = uint16_t(i);
    return true;
  }
  int decode(BitReader& br) const {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; ++len) {
      code |= int(br.get(1));
      const int count = counts[size_t(len)];
      if (code - count < first) return symbols[size_t(index + (code - first))];
      index += count;
      first += count;
      first <<= 1;
      code <<= 1;
      if (br.bad) return -1;
    }
    return -1;
  }
};

constexpr uint16_t kLenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr uint16_t kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

bool inflate_block(BitReader& br, std::vector<uint8_t>& out, const Huffman& lit, const Huffman& dist) {
  for (;;) {
    const int s = lit.decode(br);
    if (s < 0 || br.bad) return false;
    if (s < 256) {
      out.push_back(uint8_t(s));
    } else if (s == 256) {
      return true;
    } else {
      const int li = s - 257;
      if (li >= 29) return false;
      const size_t len = kLenBase[li] + br.get(kLenExtra[li]);
      const int di = dist.decode(br);
      if (di < 0 || di >= 30) return false;
      const size_t d = kDistBase[di] + br.get(kDistExtra[di]);
      if (d > out.size()) return false;
      const size_t from = out.size() - d;
      for (size_t k = 0; k < len; ++k) out.push_back(out[from + k]);
    }
  }
}

uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }

}  // namespace

std::optional<std::vector<uint8_t>> inflate_zlib(std::span<const uint8_t> data) {
  if (data.size() < 2 || (data[0] & 0x0F) != 8 || ((data[0] << 8) | data[1]) % 31 != 0) return std::nullopt;
  BitReader br{data.subspan(2)};
  std::vector<uint8_t> out;
  for (bool last = false; !last;) {
    last = br.get(1) != 0;
    const uint32_t type = br.get(2);
    if (br.bad) return std::nullopt;
    if (type == 0) {
      br.bits = 0;
      br.count = 0;
      if (br.pos + 4 > br.d.size()) return std::nullopt;
      const uint32_t len = uint32_t(br.d[br.pos]) | uint32_t(br.d[br.pos + 1]) << 8;
      br.pos += 4;
      if (br.pos + len > br.d.size()) return std::nullopt;
      out.insert(out.end(), br.d.begin() + std::ptrdiff_t(br.pos), br.d.begin() + std::ptrdiff_t(br.pos + len));
      br.pos += len;
    } else if (type == 1) {
      uint8_t lengths[320];
      for (int i = 0; i < 144; ++i) lengths[i] = 8;
      for (int i = 144; i < 256; ++i) lengths[i] = 9;
      for (int i = 256; i < 280; ++i) lengths[i] = 7;
      for (int i = 280; i < 288; ++i) lengths[i] = 8;
      Huffman lit, dist;
      lit.build(lengths, 288);
      uint8_t dl[30];
      std::memset(dl, 5, sizeof(dl));
      dist.build(dl, 30);
      if (!inflate_block(br, out, lit, dist)) return std::nullopt;
    } else if (type == 2) {
      const int hlit = int(br.get(5)) + 257, hdist = int(br.get(5)) + 1, hclen = int(br.get(4)) + 4;
      static const int kOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
      uint8_t cl[19] = {};
      for (int i = 0; i < hclen; ++i) cl[kOrder[i]] = uint8_t(br.get(3));
      Huffman ch;
      ch.build(cl, 19);
      uint8_t lengths[320] = {};
      for (int i = 0; i < hlit + hdist;) {
        const int s = ch.decode(br);
        if (s < 0 || br.bad) return std::nullopt;
        if (s < 16) {
          lengths[i++] = uint8_t(s);
        } else {
          int rep = 0;
          uint8_t v = 0;
          if (s == 16) {
            if (i == 0) return std::nullopt;
            v = lengths[i - 1];
            rep = 3 + int(br.get(2));
          } else if (s == 17) {
            rep = 3 + int(br.get(3));
          } else {
            rep = 11 + int(br.get(7));
          }
          if (i + rep > hlit + hdist) return std::nullopt;
          while (rep--) lengths[i++] = v;
        }
      }
      Huffman lit, dist;
      lit.build(lengths, hlit);
      dist.build(lengths + hlit, hdist);
      if (!inflate_block(br, out, lit, dist)) return std::nullopt;
    } else {
      return std::nullopt;
    }
  }
  return out;
}

std::optional<Image> decode_png(std::span<const uint8_t> f) {
  static const uint8_t kSig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  if (f.size() < 8 || std::memcmp(f.data(), kSig, 8) != 0) return std::nullopt;
  uint32_t w = 0, h = 0;
  int depth = 0, colour = 0, interlace = 0;
  std::vector<uint8_t> idat, palette, trns;
  for (size_t p = 8; p + 12 <= f.size();) {
    const uint32_t len = be32(&f[p]);
    if (p + 12 + len > f.size()) return std::nullopt;
    const uint8_t* type = &f[p + 4];
    const uint8_t* d = &f[p + 8];
    if (std::memcmp(type, "IHDR", 4) == 0 && len >= 13) {
      w = be32(d);
      h = be32(d + 4);
      depth = d[8];
      colour = d[9];
      interlace = d[12];
    } else if (std::memcmp(type, "PLTE", 4) == 0) {
      palette.assign(d, d + len);
    } else if (std::memcmp(type, "tRNS", 4) == 0) {
      trns.assign(d, d + len);
    } else if (std::memcmp(type, "IDAT", 4) == 0) {
      idat.insert(idat.end(), d, d + len);
    } else if (std::memcmp(type, "IEND", 4) == 0) {
      break;
    }
    p += 12 + len;
  }
  if (w == 0 || h == 0 || depth != 8 || interlace != 0) return std::nullopt;
  int channels = 0;
  switch (colour) {
    case 0: channels = 1; break;
    case 2: channels = 3; break;
    case 3: channels = 1; break;
    case 4: channels = 2; break;
    case 6: channels = 4; break;
    default: return std::nullopt;
  }
  const auto raw = inflate_zlib(idat);
  const size_t stride = size_t(w) * size_t(channels);
  if (!raw || raw->size() < (stride + 1) * h) return std::nullopt;
  std::vector<uint8_t> px(stride * h);
  const size_t bpp = size_t(channels);
  for (uint32_t y = 0; y < h; ++y) {
    const uint8_t filter = (*raw)[y * (stride + 1)];
    const uint8_t* src = &(*raw)[y * (stride + 1) + 1];
    uint8_t* row = &px[y * stride];
    const uint8_t* up = y ? &px[(y - 1) * stride] : nullptr;
    for (size_t x = 0; x < stride; ++x) {
      const int a = x >= bpp ? row[x - bpp] : 0, b = up ? up[x] : 0, c = (up && x >= bpp) ? up[x - bpp] : 0;
      int v = src[x];
      switch (filter) {
        case 0: break;
        case 1: v += a; break;
        case 2: v += b; break;
        case 3: v += (a + b) / 2; break;
        case 4: {
          const int pp = a + b - c, pa = std::abs(pp - a), pb = std::abs(pp - b), pc = std::abs(pp - c);
          v += (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
          break;
        }
        default: return std::nullopt;
      }
      row[x] = uint8_t(v);
    }
  }
  Image img;
  img.width = w;
  img.height = h;
  img.rgba.resize(size_t(w) * h * 4);
  for (size_t i = 0; i < size_t(w) * h; ++i) {
    uint8_t* o = &img.rgba[i * 4];
    const uint8_t* s = &px[i * bpp];
    switch (colour) {
      case 0: o[0] = o[1] = o[2] = s[0]; o[3] = 255; break;
      case 2: o[0] = s[0]; o[1] = s[1]; o[2] = s[2]; o[3] = 255; break;
      case 3: {
        const size_t k = s[0];
        if (k * 3 + 2 >= palette.size()) return std::nullopt;
        o[0] = palette[k * 3];
        o[1] = palette[k * 3 + 1];
        o[2] = palette[k * 3 + 2];
        o[3] = k < trns.size() ? trns[k] : 255;
        break;
      }
      case 4: o[0] = o[1] = o[2] = s[0]; o[3] = s[1]; break;
      default: std::memcpy(o, s, 4); break;
    }
  }
  return img;
}

}  // namespace geow2

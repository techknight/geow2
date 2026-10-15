#include "data/xdbf.h"

#include <cstring>

namespace geow2 {
namespace {

uint16_t be16(std::span<const uint8_t> d, size_t o) { return uint16_t(d[o] << 8 | d[o + 1]); }
uint32_t be32(std::span<const uint8_t> d, size_t o) {
  return uint32_t(d[o]) << 24 | uint32_t(d[o + 1]) << 16 | uint32_t(d[o + 2]) << 8 | d[o + 3];
}
uint64_t be64(std::span<const uint8_t> d, size_t o) { return uint64_t(be32(d, o)) << 32 | be32(d, o + 4); }

}  // namespace

Xdbf Xdbf::parse(std::span<const uint8_t> d) {
  Xdbf x;
  if (d.size() < 24 || std::memcmp(d.data(), "XDBF", 4) != 0) return x;
  const uint32_t table_len = be32(d, 8), count = be32(d, 12), free_len = be32(d, 16);
  const size_t base = 24 + size_t(table_len) * 18 + size_t(free_len) * 8;
  if (base > d.size() || count > table_len) return x;
  for (uint32_t i = 0; i < count; ++i) {
    const size_t e = 24 + size_t(i) * 18;
    const uint16_t ns = be16(d, e);
    const uint64_t id = be64(d, e + 2);
    const size_t off = base + be32(d, e + 10), size = be32(d, e + 14);
    if (off + size > d.size()) continue;
    const auto blob = d.subspan(off, size);
    if (ns == 1 && size >= 14 && std::memcmp(blob.data(), "XACH", 4) == 0) {
      const uint16_t n = be16(blob, 12);
      for (uint16_t k = 0; k < n && 14 + size_t(k + 1) * 0x24 <= size; ++k) {
        const size_t a = 14 + size_t(k) * 0x24;
        Achievement ach;
        ach.id = be16(blob, a);
        ach.name = be16(blob, a + 2);
        ach.description = be16(blob, a + 4);
        ach.unachieved = be16(blob, a + 6);
        ach.image = be32(blob, a + 8);
        ach.gamerscore = be16(blob, a + 12);
        ach.flags = be32(blob, a + 16);
        x.achievements_.push_back(ach);
      }
    } else if (ns == 2) {
      x.images_[id] = blob;
    } else if (ns == 3 && size >= 14 && std::memcmp(blob.data(), "XSTR", 4) == 0) {
      auto& table = x.strings_[int(id)];
      const uint16_t n = be16(blob, 12);
      size_t p = 14;
      for (uint16_t k = 0; k < n && p + 4 <= size; ++k) {
        const uint16_t sid = be16(blob, p), len = be16(blob, p + 2);
        if (p + 4 + len > size) break;
        table[sid] = std::string(reinterpret_cast<const char*>(blob.data() + p + 4), len);
        p += 4 + size_t(len);
      }
    }
  }
  x.valid_ = true;
  return x;
}

std::string Xdbf::string(int language, int id) const {
  const auto t = strings_.find(language);
  if (t == strings_.end()) return {};
  const auto s = t->second.find(id);
  return s == t->second.end() ? std::string() : s->second;
}

std::span<const uint8_t> Xdbf::image(uint64_t id) const {
  const auto i = images_.find(id);
  return i == images_.end() ? std::span<const uint8_t>() : i->second;
}

}  // namespace geow2

#include "extract/stfs.h"

#include <algorithm>
#include <cstring>

namespace geow2 {
namespace {
constexpr size_t kBlockSize = 0x1000;
constexpr uint32_t kBlocksPerTable = 0xAA;  // 170 data blocks per hash table
constexpr size_t kEntrySize = 0x40;
}  // namespace

StfsPackage::StfsPackage(Bytes data) : data_(std::move(data)) {
  need(data_, 0, 0x971A, "stfs header");
  if (std::memcmp(data_.data(), "LIVE", 4) && std::memcmp(data_.data(), "PIRS", 4) &&
      std::memcmp(data_.data(), "CON ", 4))
    throw FormatError("stfs: not a LIVE/PIRS/CON package");

  const uint32_t header_size = be32(&data_[0x340]);
  version_ = be32(&data_[0x358]);
  title_id_ = be32(&data_[0x360]);
  data_start_ = (header_size + kBlockSize - 1) & ~(kBlockSize - 1);

  // Volume descriptor at 0x379.
  const uint8_t flags = data_[0x37B];
  table_shift_ = (flags & 1) ? 0 : 1;  // read-only packages keep one hash table per level
  const uint16_t table_blocks = le16(&data_[0x37C]);
  const uint32_t table_block = le24(&data_[0x37E]);

  std::vector<std::string> names;  // full path of each entry, for parent lookup
  for (uint32_t b = 0; b < table_blocks; ++b) {
    const size_t off = block_offset(table_block + b);
    need(data_, off, kBlockSize, "stfs file table");
    for (size_t e = 0; e < kBlockSize / kEntrySize; ++e) {
      const uint8_t* p = &data_[off + e * kEntrySize];
      const uint8_t name_flags = p[0x28];
      if (name_flags == 0) continue;
      std::string name(reinterpret_cast<const char*>(p), name_flags & 0x3F);
      const int16_t parent = int16_t(be16(p + 0x32));
      std::string path = name;
      if (parent >= 0) {
        if (size_t(parent) >= names.size()) throw FormatError("stfs: bad parent index");
        path = names[size_t(parent)] + "/" + name;
      }
      names.push_back(path);
      if (name_flags & 0x80) continue;  // directory
      StfsFile f;
      f.path = path;
      f.contiguous = (name_flags & 0x40) != 0;
      f.start_block = le24(p + 0x2F);
      f.size = be32(p + 0x34);
      files_.push_back(f);
    }
  }
}

size_t StfsPackage::block_offset(uint32_t block) const {
  // Each run of 170 data blocks is preceded by its hash table; higher levels
  // add one table per 170^2 and 170^3 blocks.
  uint64_t base = kBlocksPerTable;
  uint64_t adjusted = block;
  for (int level = 0; level < 3; ++level) {
    adjusted += (uint64_t(block) + (base << table_shift_)) / (base << table_shift_);
    if (block < base) break;
    base *= kBlocksPerTable;
  }
  return data_start_ + size_t(adjusted) * kBlockSize;
}

Bytes StfsPackage::read(const StfsFile& f) const {
  if (!f.contiguous && f.size)
    throw FormatError("stfs: " + f.path + " is not stored contiguously (not supported)");
  Bytes out;
  out.reserve(f.size);
  uint32_t block = f.start_block;
  while (out.size() < f.size) {
    const size_t off = block_offset(block++);
    const size_t n = std::min<size_t>(kBlockSize, f.size - out.size());
    need(data_, off, n, "stfs file data");
    out.insert(out.end(), data_.begin() + std::ptrdiff_t(off), data_.begin() + std::ptrdiff_t(off + n));
  }
  return out;
}

}  // namespace geow2

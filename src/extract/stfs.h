#pragma once

#include <string>
#include <vector>

#include "extract/bytes.h"

namespace geow2 {

struct StfsFile {
  std::string path;  // '/'-separated, relative to the package root
  uint32_t size = 0;
  uint32_t start_block = 0;
  bool contiguous = false;
};

// Read-only STFS package (LIVE/PIRS/CON) as used for Xbox Live Arcade titles.
// Supports the read-only ("one hash table per level") layout with contiguous
// files, which is what the Geometry Wars 2 package uses.
class StfsPackage {
 public:
  explicit StfsPackage(Bytes data);

  uint32_t title_id() const { return title_id_; }
  uint32_t version() const { return version_; }
  const std::vector<StfsFile>& files() const { return files_; }
  Bytes read(const StfsFile& f) const;

 private:
  size_t block_offset(uint32_t block) const;

  Bytes data_;
  uint32_t title_id_ = 0;
  uint32_t version_ = 0;
  size_t data_start_ = 0;
  int table_shift_ = 0;
  std::vector<StfsFile> files_;
};

}  // namespace geow2

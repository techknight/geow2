#pragma once

#include <string>
#include <vector>

#include "extract/bytes.h"

namespace geow2 {

struct XexImport {
  std::string library;
  uint32_t record_address = 0;  // address of the import record (thunk or variable)
  uint32_t ordinal = 0;
  bool is_thunk = false;        // second record of a function import: the call stub
};

struct XexPageRun {
  uint32_t address = 0;
  uint32_t size = 0;
  uint32_t info = 0;  // 1 = code, 2 = data, 3 = read-only data (as the security info names them)
};

struct XexImage {
  uint32_t load_address = 0;
  uint32_t entry_point = 0;
  uint32_t image_flags = 0;
  std::string original_pe_name;
  Bytes image;  // memory image, image.size() == image_size
  std::vector<XexPageRun> page_runs;
  std::vector<XexImport> imports;
};

// Decodes a retail XEX2: decrypts (AES-128, publicly known retail key),
// decompresses (none, "basic" zero-fill, or "normal" LZX) and checks every
// block hash and page digest the executable carries.
XexImage xex_load(std::span<const uint8_t> xex);

}  // namespace geow2

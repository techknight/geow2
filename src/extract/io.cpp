#include <filesystem>
#include <fstream>

#include "extract/bytes.h"

namespace geow2 {

Bytes read_file(const std::filesystem::path& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) throw std::runtime_error("cannot open " + path.string());
  return Bytes(std::istreambuf_iterator<char>(f), {});
}

void write_file(const std::filesystem::path& path, std::span<const uint8_t> data) {
  if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
  std::ofstream f(path, std::ios::binary);
  if (!f) throw std::runtime_error("cannot write " + path.string());
  f.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
}

}  // namespace geow2

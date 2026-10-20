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

bool replace_file(const std::filesystem::path& path, std::span<const uint8_t> data, [[maybe_unused]] bool owner_only) {
  std::error_code ec;
  if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
  std::filesystem::path tmp = path;
  tmp += ".tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
#ifndef _WIN32
    // Before anything is written into it.
    if (owner_only && f)
      std::filesystem::permissions(tmp, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                   std::filesystem::perm_options::replace, ec);
#endif
    f.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
    f.flush();
    if (!f) {
      f.close();
      std::filesystem::remove(tmp, ec);
      return false;
    }
  }
  std::filesystem::rename(tmp, path, ec);
  if (ec) {
    std::filesystem::remove(tmp, ec);
    return false;
  }
  return true;
}

}  // namespace geow2

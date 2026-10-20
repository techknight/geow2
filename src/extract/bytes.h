#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace geow2 {

using Bytes = std::vector<uint8_t>;

inline uint32_t be32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
inline uint16_t be16(const uint8_t* p) { return uint16_t((p[0] << 8) | p[1]); }
inline uint32_t le24(const uint8_t* p) { return p[0] | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16); }
inline uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }

struct FormatError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

// Throws FormatError unless [off, off + len) lies inside data.
inline void need(std::span<const uint8_t> data, size_t off, size_t len, const char* what) {
  if (off > data.size() || len > data.size() - off) throw FormatError(std::string(what) + ": truncated");
}

Bytes read_file(const std::filesystem::path& path);
void write_file(const std::filesystem::path& path, std::span<const uint8_t> data);
// Writes `data` beside `path` and then renames it over `path`, so a failed
// or interrupted write leaves the old file whole. Creates the folder;
// returns false (and keeps the old file) on any failure. `owner_only`:
// readable and writable by the owner alone (a file holding a secret; on
// Windows the user's profile folder is already private).
bool replace_file(const std::filesystem::path& path, std::span<const uint8_t> data, bool owner_only = false);

}  // namespace geow2

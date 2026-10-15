#pragma once

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace geow2 {

// An Xbox title's achievement and string database (XDBF, the SPA
// resource): a header, an entry table of (namespace, id, offset, size)
// and the data. Namespace 1 holds the sections (XACH achievements, ...),
// 2 the images (PNG), 3 the string tables (XSTR) by language.
class Xdbf {
 public:
  struct Achievement {
    int id = 0;
    int name = 0, description = 0, unachieved = 0;  // string ids
    uint32_t image = 0;
    int gamerscore = 0;
    uint32_t flags = 0;
  };

  // Null-equivalent (empty) when `data` is not an XDBF.
  static Xdbf parse(std::span<const uint8_t> data);
  bool valid() const { return valid_; }

  const std::vector<Achievement>& achievements() const { return achievements_; }
  // A string of a language's table (1 English, 3 German, 4 French, 5
  // Spanish, 6 Italian), UTF-8; "" when missing.
  std::string string(int language, int id) const;
  // An image's bytes (PNG), empty when missing.
  std::span<const uint8_t> image(uint64_t id) const;

 private:
  bool valid_ = false;
  std::vector<Achievement> achievements_;
  std::map<int, std::map<int, std::string>> strings_;
  std::map<uint64_t, std::span<const uint8_t>> images_;
};

}  // namespace geow2

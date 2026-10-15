#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace geow2 {

// Localized text from a `Text-<lang>.loc` file (format: docs/formats/xtlo.md).
// Lookups follow the game's loader (sub_82094358, sub_82094038,
// sub_820940E0): ID name -> hash in the name-sorted ID table, then hash ->
// UTF-16 string in the hash-sorted string table of the chosen language.
class TextTable {
 public:
  // Throws FormatError when the file is not XTLO version 1 or the language
  // is missing (the game then keeps its built-in strings).
  TextTable(std::span<const uint8_t> file, const std::string& language);

  std::optional<std::u16string> find(const std::string& id) const;
  const std::vector<std::string>& ids() const { return id_names_; }

 private:
  struct Id {
    std::string name;
    uint32_t hash;
  };
  struct Str {
    uint32_t hash;
    std::u16string text;
  };
  std::vector<std::string> id_names_;
  std::vector<Id> ids_;      // sorted by name (as stored)
  std::vector<Str> strings_; // sorted by hash (as stored)
};

}  // namespace geow2

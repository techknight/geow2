#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace geow2 {

class TextTable;

// The game's text slots by id (sub_820826D8): the id table at 0x8218E1F0
// in the user's image names each id's string ({id, name pointer} pairs),
// the strings come from the language's text table, converted to single
// bytes as sub_82082720 does (Latin-1).
class UiText {
 public:
  static UiText load(std::span<const uint8_t> image, const TextTable& table);
  // Id 0..0xB0; anything else is "?? LocId out of range ??".
  const std::string& operator()(int id) const;
  // The id's name in the table (ID_...), "" when unknown.
  const std::string& name(int id) const;

 private:
  std::vector<std::string> text_, names_;
};

}  // namespace geow2

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

// UTF-8 to the font's single bytes (Latin-1; anything past U+00FF '?').
std::string latin1_from_utf8(const std::string& utf8);

// A player's name for the font: Latin-1, with control bytes (which the
// font draws as button and ship icons, or uses to change the text size)
// as spaces.
std::string display_name(const std::string& utf8);

}  // namespace geow2

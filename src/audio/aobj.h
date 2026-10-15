#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace geow2::audio {

// One component record of an AOBJ file (format: docs/formats/aobj.md).
// `body` holds the words after the type word; body[0] is always the
// component's name as an offset into the string table.
struct AobjRecord {
  uint32_t type = 0;
  uint32_t offset = 0;  // file offset of the type word
  std::vector<uint32_t> body;
};

// A parsed AOBJ file: the definition of one audio object (a component
// graph). The game's header check is sub_82125F80; the record walk follows
// the sizer sub_82105370.
struct Aobj {
  std::string name;  // the object name from the string table
  std::vector<AobjRecord> records;
  std::vector<char> strings;  // the string table, NUL-terminated names

  // The NUL-terminated string at `offset` in the string table; throws
  // FormatError when it runs past the end.
  std::string string_at(uint32_t offset) const;
};

// Number of body words (after the type word) of a record of `type`, given
// the words that follow the type word (sub_82105370). `words` must hold at
// least the fixed part the size depends on; returns 0 and sets `known` false
// for a type the game does not know.
size_t aobj_body_words(uint32_t type, std::span<const uint32_t> words, bool& known);

// Throws FormatError on anything the game's loader would reject (magic,
// unknown component type) or that does not fit in the file.
Aobj parse_aobj(std::span<const uint8_t> file);

// The float stored in a record word (big-endian IEEE single, already
// byte-swapped into the word).
float word_float(uint32_t w);

}  // namespace geow2::audio

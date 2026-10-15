#include "data/xtlo.h"

#include <algorithm>
#include <cstring>

#include "extract/bytes.h"

namespace geow2 {
namespace {

constexpr uint32_t kMaxBlock = 0x400000;  // sub_820942E0 refuses larger blocks

std::span<const uint8_t> block(std::span<const uint8_t> file, uint32_t offset, uint32_t size, const char* what) {
  if (size > kMaxBlock) throw FormatError(std::string("xtlo: ") + what + " too large");
  need(file, offset, size, what);
  return file.subspan(offset, size);
}

std::string c_string(std::span<const uint8_t> b, size_t off) {
  std::string s;
  while (off < b.size() && b[off]) s += char(b[off++]);
  if (off >= b.size()) throw FormatError("xtlo: unterminated ID name");
  return s;
}

std::u16string utf16be(std::span<const uint8_t> b, size_t off) {
  std::u16string s;
  for (;; off += 2) {
    need(b, off, 2, "xtlo string");
    const char16_t c = char16_t(be16(&b[off]));
    if (!c) return s;
    s += c;
  }
}

}  // namespace

TextTable::TextTable(std::span<const uint8_t> file, const std::string& language) {
  need(file, 0, 16, "xtlo header");
  if (std::memcmp(file.data(), "XTLO", 4) || be32(&file[4]) != 1) throw FormatError("xtlo: not XTLO version 1");
  const auto ids = block(file, be32(&file[8]), be32(&file[12]), "ID block");
  need(ids, 0, 12, "ID block");

  // Language list: 0x18-byte entries, name compared with strcmp (sub_82094148).
  const uint32_t lang_off = be32(&ids[0]);
  const uint32_t lang_count = be32(&ids[4]);
  std::span<const uint8_t> strings;
  for (uint32_t i = 0; i < lang_count; ++i) {
    const size_t e = lang_off + size_t(i) * 0x18;
    need(ids, e, 0x18, "language list");
    if (c_string(ids.subspan(e, 0x10), 0) == language) {
      strings = block(file, be32(&ids[e + 0x10]), be32(&ids[e + 0x14]), "string block");
      break;
    }
  }
  if (strings.empty()) throw FormatError("xtlo: language " + language + " not present");

  // ID table: count, offset of the first entry, then 8-byte entries of
  // (hash, name offset from the table start).
  const uint32_t table = be32(&ids[8]);
  need(ids, table, 8, "ID table");
  const uint32_t count = be32(&ids[table]);
  const uint32_t first = table + be32(&ids[table + 4]);
  for (uint32_t i = 0; i < count; ++i) {
    need(ids, first + size_t(i) * 8, 8, "ID table");
    const uint8_t* p = &ids[first + size_t(i) * 8];
    Id id{c_string(ids, table + size_t(be32(p + 4))), be32(p)};
    id_names_.push_back(id.name);
    ids_.push_back(std::move(id));
  }

  // String table: count, offset of the first entry, then 8-byte entries of
  // (hash, string offset from the block start).
  need(strings, 0, 8, "string table");
  const uint32_t scount = be32(&strings[0]);
  const uint32_t sfirst = be32(&strings[4]);
  for (uint32_t i = 0; i < scount; ++i) {
    need(strings, sfirst + size_t(i) * 8, 8, "string table");
    const uint8_t* p = &strings[sfirst + size_t(i) * 8];
    strings_.push_back({be32(p), utf16be(strings, be32(p + 4))});
  }
}

std::optional<std::u16string> TextTable::find(const std::string& id) const {
  // Both searches are binary searches in the game, so they rely on the
  // stored order; we do the same.
  auto it = std::lower_bound(ids_.begin(), ids_.end(), id, [](const Id& a, const std::string& b) { return a.name < b; });
  if (it == ids_.end() || it->name != id) return std::nullopt;
  const uint32_t hash = it->hash;
  auto st = std::lower_bound(strings_.begin(), strings_.end(), hash,
                             [](const Str& a, uint32_t h) { return a.hash < h; });
  if (st == strings_.end() || st->hash != hash) return std::nullopt;
  return st->text;
}

}  // namespace geow2

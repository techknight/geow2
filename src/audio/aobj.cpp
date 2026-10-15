#include "audio/aobj.h"

#include <bit>
#include <cstring>

#include "extract/bytes.h"

namespace geow2::audio {

float word_float(uint32_t w) { return std::bit_cast<float>(w); }

std::string Aobj::string_at(uint32_t offset) const {
  if (offset >= strings.size()) throw FormatError("aobj: string offset out of range");
  const void* end = std::memchr(strings.data() + offset, 0, strings.size() - offset);
  if (!end) throw FormatError("aobj: unterminated string");
  return std::string(strings.data() + offset, static_cast<const char*>(end));
}

size_t aobj_body_words(uint32_t type, std::span<const uint32_t> w, bool& known) {
  known = true;
  // p(k): word k counted from the type word, as in sub_82105370.
  auto p = [&](size_t k) -> uint64_t {
    if (k - 1 >= w.size()) throw FormatError("aobj: truncated component record");
    return w[k - 1];
  };
  uint64_t n = 0;
  switch (type) {
    case 0x03: case 0x06: case 0x09: case 0x0C: case 0x0E: case 0x10: case 0xB8:
      n = 1; break;
    case 0x00: case 0x02: case 0x07: case 0x08: case 0x12: case 0x0D: case 0x0F: case 0x11: case 0xB9:
      n = 2; break;
    case 0x26: case 0x29: case 0x2D: case 0xAC: case 0xB1:
      n = 3; break;
    case 0x01: case 0x04: case 0xAB: case 0xAF: case 0x61: case 0x64:
      n = 4; break;
    case 0x18: case 0x23:
      n = 6; break;
    case 0x1D:
      n = 7; break;
    case 0xB0:
      n = 8; break;
    case 0x1E: case 0x27: case 0x73:
      n = 9; break;
    case 0x1A: case 0xAE:
      n = 11; break;
    case 0x6D: case 0x6E: case 0x6F:
      n = 13; break;
    case 0x76: case 0x77:
      n = 14; break;
    case 0x2E:
      n = 15; break;
    case 0x7F: case 0x80: case 0x81: case 0x82: case 0x83: case 0x94: case 0x95: case 0x96: case 0xAD: case 0xBC:
      n = 16; break;
    case 0x30:
      n = 17; break;
    case 0x16:
      n = 18; break;
    case 0x70:
      n = 19; break;
    case 0x85: case 0x86: case 0x87: case 0x8B: case 0x8D: case 0x8E: case 0x90: case 0x91: case 0x92: case 0x93:
      n = 22; break;
    case 0x17:
      n = 24; break;
    case 0xB2: case 0xB3:
      n = 31; break;
    case 0x15:
      n = 48; break;
    case 0x66:
      n = 0; break;
    case 0x13:
      n = 7 + 4 * p(7); break;
    case 0x1F: case 0x20: case 0x24: case 0x25:
      n = 2 + 3 * p(2); break;
    case 0x31:
      n = 7 + 3 * p(7); break;
    case 0x34: case 0x35:
      n = 18 + p(18); break;
    case 0x36:
      n = 21 + p(4) + p(21); break;
    case 0x38: case 0x39:
      n = 20 + p(20); break;
    case 0x3D: case 0x40: case 0x45: case 0xB7:
      n = 6 + p(5) + 4 * p(6); break;
    case 0x3E: case 0xB6:
      n = 9 + p(8) + 4 * p(9); break;
    case 0x3F:
      n = 15 + p(14) + 4 * p(15); break;
    case 0x41:
      n = 12 + p(11) + 4 * p(12); break;
    case 0x42:
      n = 24 + p(23) + 4 * p(24); break;
    case 0x46:
      n = 3 + p(2) + 4 * p(3); break;
    case 0xA9:
      n = 3 + p(3); break;
    case 0xAA:
      n = 5 + 12 * p(2); break;
    case 0xB5:
      n = 18 + p(17) + 4 * p(18); break;
    case 0xBA:
      n = 30 + p(29) + 4 * p(30); break;
    case 0xBB:
      n = 42 + p(41) + 4 * p(42); break;
    default:
      known = false;
      return 0;
  }
  if (n > w.size()) throw FormatError("aobj: truncated component record");
  return size_t(n);
}

Aobj parse_aobj(std::span<const uint8_t> file) {
  need(file, 0, 0x14, "aobj header");
  // sub_82125F80: magic, else error 5.
  if (be32(file.data()) != 0x414F424A) throw FormatError("aobj: bad magic");
  const uint32_t strings_at = be32(file.data() + 0x08);
  const uint32_t name_at = be32(file.data() + 0x0C);
  const uint32_t count = be32(file.data() + 0x10);
  if (strings_at < 0x14 || strings_at > file.size() || (strings_at - 0x14) % 4 != 0)
    throw FormatError("aobj: bad string table offset");

  Aobj a;
  a.strings.assign(file.begin() + strings_at, file.end());
  a.strings.push_back(0);  // a name running to the end of the file still terminates
  std::vector<uint32_t> words((strings_at - 0x14) / 4);
  for (size_t i = 0; i < words.size(); ++i) words[i] = be32(file.data() + 0x14 + 4 * i);

  size_t at = 0;
  a.records.reserve(count);
  for (uint32_t k = 0; k < count; ++k) {
    if (at >= words.size()) throw FormatError("aobj: component records run past the string table");
    const uint32_t type = words[at];
    bool known = false;
    const std::span<const uint32_t> rest(words.data() + at + 1, words.size() - at - 1);
    const size_t n = aobj_body_words(type, rest, known);
    if (!known) throw FormatError("aobj: unknown component type");
    AobjRecord r;
    r.type = type;
    r.offset = uint32_t(0x14 + 4 * at);
    r.body.assign(rest.begin(), rest.begin() + n);
    a.records.push_back(std::move(r));
    at += 1 + n;
  }
  a.name = a.string_at(name_at);
  return a;
}

}  // namespace geow2::audio

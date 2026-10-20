#include "game/ui_text.h"

#include "data/xtlo.h"

namespace geow2 {
namespace {

constexpr uint32_t kBase = 0x82000000;
constexpr uint32_t kIdTable = 0x8218E1F0;
constexpr int kSlots = 0xB1;

uint32_t be32(std::span<const uint8_t> b, size_t o) {
  return uint32_t(b[o]) << 24 | uint32_t(b[o + 1]) << 16 | uint32_t(b[o + 2]) << 8 | uint32_t(b[o + 3]);
}

}  // namespace

UiText UiText::load(std::span<const uint8_t> image, const TextTable& table) {
  UiText t;
  t.text_.assign(kSlots, std::string());
  t.names_.assign(kSlots, std::string());
  for (uint32_t i = 0;; ++i) {
    const size_t o = size_t(kIdTable - kBase) + i * 8;
    if (o + 8 > image.size()) break;
    const uint32_t id = be32(image, o), name = be32(image, o + 4);
    if (name < kBase || name - kBase >= image.size()) break;
    if (id >= uint32_t(kSlots)) continue;
    std::string n;
    for (size_t p = name - kBase; p < image.size() && image[p]; ++p) n.push_back(char(image[p]));
    t.names_[id] = n;
    if (const auto s = table.find(n))
      for (char16_t c : *s) t.text_[id].push_back(c <= 0xFF ? char(c) : '?');
  }
  return t;
}

const std::string& UiText::operator()(int id) const {
  static const std::string kOut = "?? LocId out of range ??";
  if (id < 0 || id >= kSlots || text_.empty()) return kOut;
  return text_[size_t(id)];
}

const std::string& UiText::name(int id) const {
  static const std::string kNone;
  if (id < 0 || id >= kSlots || names_.empty()) return kNone;
  return names_[size_t(id)];
}

std::string latin1_from_utf8(const std::string& u) {
  std::string out;
  for (size_t i = 0; i < u.size();) {
    const unsigned char c = static_cast<unsigned char>(u[i]);
    if (c < 0x80) {
      out.push_back(char(c));
      ++i;
    } else if ((c & 0xE0) == 0xC0 && i + 1 < u.size()) {
      const unsigned v = (c & 0x1Fu) << 6 | (static_cast<unsigned char>(u[i + 1]) & 0x3Fu);
      out.push_back(v <= 0xFF ? char(v) : '?');
      i += 2;
    } else {
      out.push_back('?');
      ++i;
      while (i < u.size() && (static_cast<unsigned char>(u[i]) & 0xC0) == 0x80) ++i;
    }
  }
  return out;
}

std::string display_name(const std::string& utf8) {
  std::string s = latin1_from_utf8(utf8);
  for (char& c : s)
    if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) c = ' ';
  return s;
}

}  // namespace geow2

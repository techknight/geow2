// Unit tests for the data-format readers, on small files built here (no game data).

#include <cstdio>
#include <string>

#include "data/xtlo.h"
#include "extract/bytes.h"

using namespace geow2;

static int failures = 0;

static void expect(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

static void put32(Bytes& b, size_t at, uint32_t v) {
  if (b.size() < at + 4) b.resize(at + 4);
  for (int i = 0; i < 4; ++i) b[at + i] = uint8_t(v >> (24 - 8 * i));
}

static void put_str(Bytes& b, size_t at, const std::string& s) {
  if (b.size() < at + s.size() + 1) b.resize(at + s.size() + 1);
  for (size_t i = 0; i < s.size(); ++i) b[at + i] = uint8_t(s[i]);
  b[at + s.size()] = 0;
}

static void put_u16(Bytes& b, size_t at, const std::u16string& s) {
  if (b.size() < at + s.size() * 2 + 2) b.resize(at + s.size() * 2 + 2);
  for (size_t i = 0; i <= s.size(); ++i) {
    const char16_t c = i < s.size() ? s[i] : 0;
    b[at + i * 2] = uint8_t(c >> 8);
    b[at + i * 2 + 1] = uint8_t(c);
  }
}

static void test_xtlo() {
  // ID block at 0x10: language list at +0x0C, ID table at +0x24.
  Bytes f;
  put_str(f, 0, "XTLO");
  put32(f, 4, 1);
  put32(f, 8, 0x10);
  const size_t id = 0x10;
  put32(f, id + 0x00, 0x0C);
  put32(f, id + 0x04, 1);
  put32(f, id + 0x08, 0x24);
  put_str(f, id + 0x0C, "english");
  // ID table: two IDs sorted by name; hashes deliberately not in name order.
  const size_t t = id + 0x24;
  put32(f, t + 0, 2);
  put32(f, t + 4, 8);
  put32(f, t + 8, 0x50);   // ID_A -> hash 0x50
  put32(f, t + 12, 0x18);
  put32(f, t + 16, 0x30);  // ID_B -> hash 0x30
  put32(f, t + 20, 0x1D);
  put_str(f, t + 0x18, "ID_A");
  put_str(f, t + 0x1D, "ID_B");
  const uint32_t id_size = uint32_t(f.size() - id);
  put32(f, 12, id_size);
  // String block, sorted by hash.
  const size_t s = (f.size() + 3) & ~size_t(3);
  put32(f, s + 0, 2);
  put32(f, s + 4, 8);
  put32(f, s + 8, 0x30);
  put32(f, s + 12, 0x18);
  put32(f, s + 16, 0x50);
  put32(f, s + 20, 0x20);
  put_u16(f, s + 0x18, u"bee");
  put_u16(f, s + 0x20, u"ay");
  put32(f, id + 0x0C + 0x10, uint32_t(s));
  put32(f, id + 0x0C + 0x14, uint32_t(f.size() - s));

  TextTable tt(f, "english");
  expect(tt.ids().size() == 2, "xtlo id count");
  expect(tt.find("ID_A") == std::u16string(u"ay"), "xtlo ID_A");
  expect(tt.find("ID_B") == std::u16string(u"bee"), "xtlo ID_B");
  expect(!tt.find("ID_C"), "xtlo missing id");

  bool threw = false;
  try {
    TextTable(f, "german");
  } catch (const FormatError&) {
    threw = true;
  }
  expect(threw, "xtlo missing language throws");
}

int main() {
  test_xtlo();
  std::printf(failures ? "%d failure(s)\n" : "all tests passed\n", failures);
  return failures ? 1 : 0;
}

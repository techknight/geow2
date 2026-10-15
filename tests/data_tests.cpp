// Unit tests for the data-format readers, on small files built here (no game data).

#include <cstdio>
#include <string>

#include "data/flask.h"
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

static void put16(Bytes& b, size_t at, uint16_t v) {
  if (b.size() < at + 2) b.resize(at + 2);
  b[at] = uint8_t(v >> 8);
  b[at + 1] = uint8_t(v);
}

static void test_flask() {
  // Version 1 (plain names): types f32 and S { a: f32 @0, b: f32 @4 };
  // one record of type S whose object holds a = 1.5, b = -2.0.
  Bytes f;
  put_str(f, 0, "FLSK");
  put32(f, 4, 1);
  const size_t types = 0x48, fields = types + 24, pool = fields + 16, recs = 0x80, objs = 0x88, chunks = 0x98,
               blob = 0xA0;
  const std::string names = std::string("f32") + '\0' + "S" + '\0' + "a" + '\0' + "b" + '\0';
  const uint32_t sec[8][2] = {{uint32_t(types), 2}, {0x48, 0}, {uint32_t(fields), 2}, {uint32_t(pool), uint32_t(names.size())},
                              {uint32_t(recs), 1},  {uint32_t(objs), 1}, {uint32_t(chunks), 1}, {uint32_t(blob), 8}};
  for (int i = 0; i < 8; ++i) {
    put32(f, 8 + i * 8, sec[i][0]);
    put32(f, 12 + i * 8, sec[i][1]);
  }
  // f32: name 0, no fields/bases, kind 0, atom 10, size 4. S: name 4, 2 fields, kind 3, size 8.
  const uint16_t t0[6] = {0, 0, 0, 0, 10, 4}, t1[6] = {4, 2, 0, 3, 0, 8};
  for (int i = 0; i < 6; ++i) {
    put16(f, types + i * 2, t0[i]);
    put16(f, types + 12 + i * 2, t1[i]);
  }
  put16(f, fields + 0, 6);   // a
  put16(f, fields + 2, 0);   // f32
  put16(f, fields + 4, 0);   // offset 0
  put16(f, fields + 6, 0);   // kind 0, not an array
  put16(f, fields + 8, 8);   // b
  put16(f, fields + 10, 0);
  put16(f, fields + 12, 4);
  put16(f, fields + 14, 0);
  for (size_t i = 0; i < names.size(); ++i) {
    if (f.size() <= pool + i) f.resize(pool + i + 1);
    f[pool + i] = uint8_t(names[i]);
  }
  put32(f, recs, 0x1234);
  put16(f, recs + 4, 1);
  put16(f, recs + 6, 0);
  put32(f, objs, 0);
  put32(f, objs + 4, 1);
  put32(f, objs + 8, 8);
  put32(f, objs + 12, 0);
  put16(f, chunks, 1);
  put16(f, chunks + 2, 1);
  put16(f, chunks + 4, 0);
  put32(f, blob, 0x3FC00000);      // 1.5
  put32(f, blob + 4, 0xC0000000);  // -2.0

  Flask db(f);
  expect(db.types().size() == 2 && db.records().size() == 1, "flask counts");
  const FlaskValue v = db.decode(0);
  const auto* s = std::get_if<std::shared_ptr<FlaskStruct>>(&v.v);
  expect(s && (*s)->type == "S", "flask root type");
  if (s) {
    const FlaskValue* a = (*s)->find("a");
    const FlaskValue* b = (*s)->find("b");
    expect(a && std::get<double>(a->v) == 1.5, "flask field a");
    expect(b && std::get<double>(b->v) == -2.0, "flask field b");
  }

  // Obfuscation round trip: encode with the inverse of sub_8212E0A8's step.
  const std::string plain = std::string("Model") + '\0';
  Bytes enc;
  uint8_t key = 0xCC;
  for (size_t i = 0; i + 1 < plain.size(); ++i) {
    const uint8_t o = uint8_t(plain[i]);
    enc.push_back(uint8_t(uint8_t(key - o) ^ 0xB3));
    key = uint8_t(key - uint8_t(i) + o);
  }
  enc.push_back(0);
  expect(flask_deobfuscate(enc) == plain, "flask deobfuscate");
}

int main() {
  test_xtlo();
  test_flask();
  std::printf(failures ? "%d failure(s)\n" : "all tests passed\n", failures);
  return failures ? 1 : 0;
}

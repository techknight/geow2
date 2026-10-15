// Unit tests for the data-format readers, on small files built here (no game data).

#include <cstdio>
#include <cstring>
#include <vector>
#include <string>

#include "data/flask.h"
#include "data/png_read.h"
#include "data/tga.h"
#include "data/xdbf.h"
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

// Synthetic images and streams (made with Python's zlib; no game data).
void test_images() {
  static const uint8_t kPng[] = {
      0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
      0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x02, 0x08, 0x06, 0x00, 0x00, 0x00, 0x9D, 0x74, 0x66,
      0x1A, 0x00, 0x00, 0x00, 0x21, 0x49, 0x44, 0x41, 0x54, 0x78, 0xDA, 0x63, 0xE4, 0x12, 0x91, 0xFB,
      0x2F, 0x27, 0x27, 0xD7, 0x08, 0xC4, 0x0D, 0x2C, 0xDF, 0xDF, 0x3D, 0x65, 0xBD, 0xFB, 0xE9, 0x25,
      0xCB, 0xC3, 0x1B, 0xE7, 0x79, 0x00, 0x72, 0x4B, 0x0B, 0x15, 0x6F, 0x7F, 0x41, 0xF1, 0x00, 0x00,
      0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82,
  };
  const auto img = decode_png(kPng);
  expect(img && img->width == 3 && img->height == 2, "png size");
  if (img) {
    const uint8_t want[24] = {10, 20, 30, 255, 40, 50, 60, 128, 70, 80, 90, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    expect(std::memcmp(img->rgba.data(), want, 24) == 0, "png pixels (sub and paeth filters)");
  }
  static const uint8_t kZ[] = {
      0x78, 0xDA, 0x63, 0x60, 0xE7, 0x13, 0x95, 0x51, 0xD6, 0x32, 0xB4, 0xB0, 0x77, 0xF3, 0x0D, 0x89,
      0x4E, 0xCA, 0x2C, 0x28, 0xAF, 0x6B, 0xED, 0x99, 0x3C, 0x6B, 0xE1, 0x8A, 0xF5, 0xDB, 0xF6, 0x1E,
      0x39, 0x7D, 0xE9, 0xE6, 0x83, 0xE7, 0xEF, 0xBE, 0x32, 0x72, 0xF0, 0x8B, 0xC9, 0xAA, 0x68, 0x1B,
      0x59, 0x3A, 0xB8, 0xFB, 0x85, 0xC6, 0x24, 0x67, 0x15, 0x56, 0xD4, 0xB7, 0xF5, 0x4E, 0x99, 0xBD,
      0x68, 0xE5, 0x86, 0xED, 0xFB, 0x8E, 0x9E, 0xB9, 0x7C, 0xEB, 0xE1, 0x8B, 0xF7, 0xDF, 0x98, 0x38,
      0x05, 0xC4, 0xE5, 0x54, 0x75, 0x8C, 0xAD, 0x1C, 0x3D, 0xFC, 0xC3, 0x62, 0x53, 0xB2, 0x8B, 0x2A,
      0x1B, 0xDA, 0xFB, 0xA6, 0xCE, 0x59, 0xBC, 0x6A, 0xE3, 0x8E, 0xFD, 0xC7, 0xCE, 0x5E, 0xB9, 0xFD,
      0xE8, 0xE5, 0x87, 0xEF, 0xCC, 0x5C, 0x82, 0x12, 0xF2, 0x6A, 0xBA, 0x26, 0xD6, 0x4E, 0x9E, 0x01,
      0xE1, 0x71, 0xA9, 0x39, 0xC5, 0x55, 0x8D, 0x1D, 0xFD, 0xD3, 0xE6, 0x2E, 0x59, 0xBD, 0x69, 0xE7,
      0x81, 0xE3, 0xE7, 0xAE, 0xDE, 0x79, 0xFC, 0xEA, 0xE3, 0x0F, 0x16, 0x6E, 0x21, 0x49, 0x05, 0x75,
      0x3D, 0x53, 0x1B, 0x67, 0xAF, 0xC0, 0x88, 0xF8, 0xB4, 0xDC, 0x92, 0xEA, 0xA6, 0xCE, 0x09, 0xD3,
      0xE7, 0x2D, 0x5D, 0xB3, 0x79, 0xD7, 0xC1, 0x13, 0xE7, 0xAF, 0xDD, 0x7D, 0xF2, 0xFA, 0xD3, 0x4F,
      0x56, 0x1E, 0x61, 0x29, 0x45, 0x0D, 0x7D, 0x33, 0x5B, 0x17, 0xEF, 0xA0, 0xC8, 0x84, 0xF4, 0xBC,
      0xD2, 0x9A, 0xE6, 0xAE, 0x89, 0x33, 0xE6, 0x2F, 0x5B, 0xBB, 0x65, 0xF7, 0xA1, 0x93, 0x17, 0xAE,
      0xDF, 0x7B, 0xFA, 0xE6, 0xF3, 0x2F, 0x36, 0x5E, 0x11, 0x69, 0x25, 0x4D, 0x03, 0x73, 0x3B, 0x57,
      0x9F, 0xE0, 0xA8, 0xC4, 0x8C, 0xFC, 0xB2, 0xDA, 0x96, 0xEE, 0x49, 0x33, 0x17, 0x2C, 0x5F, 0xB7,
      0x75, 0xCF, 0xE1, 0x53, 0x17, 0x6F, 0xDC, 0x7F, 0xF6, 0xF6, 0x0B, 0xC3, 0xA8, 0xD7, 0x47, 0xBD,
      0x3E, 0xEA, 0xF5, 0x51, 0xAF, 0x8F, 0x7A, 0x7D, 0xD4, 0xEB, 0xA3, 0x5E, 0x1F, 0xF5, 0xFA, 0xA8,
      0xD7, 0x47, 0xBD, 0x3E, 0x54, 0xBC, 0x0E, 0x00, 0x6C, 0x39, 0xB5, 0x5E,
  };
  const auto out = inflate_zlib(kZ);
  bool same = out && out->size() == 3000;
  for (size_t i = 0; same && i < 3000; ++i) same = (*out)[i] == uint8_t((i * 7) % 251);
  expect(same, "zlib dynamic huffman");
  static const uint8_t kStored[] = {
      0x78, 0x01, 0x01, 0x05, 0x00, 0xFA, 0xFF, 0x68, 0x65, 0x6C, 0x6C, 0x6F, 0x06, 0x2C, 0x02, 0x15,
  };
  const auto s = inflate_zlib(kStored);
  expect(s && std::string(s->begin(), s->end()) == "hello", "zlib stored block");
  // A 2 x 2 RLE TGA, 24 bits, bottom-up: a run of two blue pixels (the
  // bottom row), then two raw ones (red, green).
  static const uint8_t kTga[] = {0, 0, 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 2, 0, 24, 0,
                                 0x81, 255, 0, 0, 0x01, 0, 0, 255, 0, 255, 0};
  const auto t = decode_tga(kTga);
  expect(t && t->width == 2 && t->height == 2, "tga size");
  if (t) {
    const uint8_t want[16] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 0, 0, 255, 255};
    expect(std::memcmp(t->rgba.data(), want, 16) == 0, "tga rle, flipped upright, alpha 255");
  }
}

// A minimal XDBF: one achievement and an English string table.
void test_xdbf() {
  std::vector<uint8_t> d;
  auto u16 = [&](uint32_t v) { d.push_back(uint8_t(v >> 8)); d.push_back(uint8_t(v)); };
  auto u32 = [&](uint32_t v) { u16(v >> 16); u16(v & 0xFFFF); };
  auto u64 = [&](uint64_t v) { u32(uint32_t(v >> 32)); u32(uint32_t(v)); };
  std::vector<uint8_t> ach = {'X', 'A', 'C', 'H', 0, 0, 0, 1, 0, 0, 0, 0x32, 0, 1};
  std::vector<uint8_t> e(0x24, 0);
  e[1] = 7;    // id
  e[3] = 10;   // name string
  e[5] = 11;   // description
  e[11] = 9;   // image
  e[13] = 15;  // gamerscore
  ach.insert(ach.end(), e.begin(), e.end());
  std::vector<uint8_t> str = {'X', 'S', 'T', 'R', 0, 0, 0, 1, 0, 0, 0, 0, 0, 2};
  for (auto [id, text] : {std::pair<int, const char*>{10, "Name"}, {11, "Does a thing"}}) {
    const size_t n = std::strlen(text);
    str.push_back(0);
    str.push_back(uint8_t(id));
    str.push_back(0);
    str.push_back(uint8_t(n));
    str.insert(str.end(), text, text + n);
  }
  d = {'X', 'D', 'B', 'F'};
  u32(0x10000);
  u32(2);  // entry table length
  u32(2);  // entries
  u32(1);  // free table length
  u32(0);
  u16(1);
  u64(0x58414348);
  u32(0);
  u32(uint32_t(ach.size()));
  u16(3);
  u64(1);
  u32(uint32_t(ach.size()));
  u32(uint32_t(str.size()));
  u32(0);
  u32(0);
  d.insert(d.end(), ach.begin(), ach.end());
  d.insert(d.end(), str.begin(), str.end());
  const Xdbf x = Xdbf::parse(d);
  expect(x.valid() && x.achievements().size() == 1, "xdbf achievements");
  if (!x.achievements().empty()) {
    const auto& a = x.achievements()[0];
    expect(a.id == 7 && a.gamerscore == 15 && a.image == 9, "xdbf achievement fields");
    expect(x.string(1, a.name) == "Name" && x.string(1, a.description) == "Does a thing", "xdbf strings");
  }
}

int main() {
  test_images();
  test_xdbf();
  test_xtlo();
  test_flask();
  std::printf(failures ? "%d failure(s)\n" : "all tests passed\n", failures);
  return failures ? 1 : 0;
}

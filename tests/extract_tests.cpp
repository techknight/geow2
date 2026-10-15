// Unit tests for the extractor's building blocks, using published test
// vectors only (no game data).

#include <cstdio>
#include <cstring>
#include <string>

#include "extract/aes128.h"
#include "extract/audio.h"
#include "extract/lzx.h"
#include "extract/sha1.h"

using namespace geow2;

static int failures = 0;

static void expect(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

static Bytes from_hex(const char* h) {
  Bytes b;
  for (size_t i = 0; h[i] && h[i + 1]; i += 2) b.push_back(uint8_t(std::stoi(std::string(h + i, 2), nullptr, 16)));
  return b;
}

static void test_sha1() {
  const std::string abc = "abc";
  expect(sha1_hex({reinterpret_cast<const uint8_t*>(abc.data()), abc.size()}) == "A9993E364706816ABA3E25717850C26C9CD0D89D",
         "sha1(abc)");
  expect(sha1_hex({}) == "DA39A3EE5E6B4B0D3255BFEF95601890AFD80709", "sha1(empty)");
  const std::string two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  expect(sha1_hex({reinterpret_cast<const uint8_t*>(two.data()), two.size()}) == "84983E441C3BD26EBAAE4AA1F95129E5E54670F1",
         "sha1(two blocks)");
}

static void test_aes() {
  // FIPS-197 appendix C.1.
  Bytes key = from_hex("000102030405060708090a0b0c0d0e0f");
  Bytes ct = from_hex("69c4e0d86a7b0430d8cdb78070b4c55a");
  Bytes pt = from_hex("00112233445566778899aabbccddeeff");
  uint8_t out[16];
  Aes128Decrypt(key.data()).decrypt_block(ct.data(), out);
  expect(std::memcmp(out, pt.data(), 16) == 0, "aes-128 FIPS-197 C.1");
  // NIST SP 800-38A F.2.2 (CBC-AES128 decrypt), first two blocks.
  Bytes k2 = from_hex("2b7e151628aed2a6abf7158809cf4f3c");
  Bytes iv = from_hex("000102030405060708090a0b0c0d0e0f");
  Bytes c2 = from_hex("7649abac8119b246cee98e9b12e9197d5086cb9b507219ee95db113a917678b2");
  Bytes p2 = from_hex("6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e51");
  Aes128Decrypt(k2.data()).decrypt_cbc(c2.data(), c2.size(), iv.data());
  expect(c2 == p2, "aes-128-cbc SP 800-38A F.2.2");
}

static void test_lzx_uncompressed() {
  // Hand-built stream: no E8 header (1 bit 0), one uncompressed block
  // (type 3, length 5), padding to the word boundary, R0..R2, 5 raw bytes.
  // Bits: 0 | 011 | 0000000000000000 | 00000101 -> 28 bits, then 4 pad bits.
  // Word 0 = 0011 0000 0000 0000 = 0x3000, word 1 = 0000 0000 0101 0000 = 0x0050.
  Bytes in = {0x00, 0x30, 0x50, 0x00};
  for (int r = 0; r < 3; ++r) in.insert(in.end(), {1, 0, 0, 0});
  in.insert(in.end(), {'h', 'e', 'l', 'l', 'o', 0});
  Bytes out = lzx_decompress(in, 1u << 15, 5);
  expect(std::string(out.begin(), out.end()) == "hello", "lzx uncompressed block");
}

static void be32_put(Bytes& b, uint32_t v) {
  for (int i = 3; i >= 0; --i) b.push_back(uint8_t(v >> (8 * i)));
}

static void test_bank() {
  // A made-up bank: one 16-bit PCM wave of two samples.
  Bytes f;
  for (char c : std::string("BANK")) f.push_back(uint8_t(c));
  be32_put(f, 0x34);
  be32_put(f, 4);
  be32_put(f, 1);
  f.push_back(1);
  const std::string name = "Test";
  f.insert(f.end(), name.begin(), name.end());
  f.resize(0x34, 0);
  const size_t wave = f.size();
  for (char c : std::string("WAVE")) f.push_back(uint8_t(c));
  be32_put(f, 8 + 0x34 + 0x14);
  be32_put(f, 3);
  const std::string wname = "beep";
  f.insert(f.end(), wname.begin(), wname.end());
  f.resize(wave + 8 + 0x24, 0);
  const size_t payload = wave + 8 + 0x34 + 0x14 + 8;
  be32_put(f, uint32_t(payload));
  be32_put(f, 4);
  be32_put(f, 0x3F800000);  // 1.0
  be32_put(f, 0x19);
  be32_put(f, 48000);
  be32_put(f, 16);
  be32_put(f, 1);
  f.resize(wave + 8 + 0x34 + 0x14, 0);
  for (char c : std::string("DATA")) f.push_back(uint8_t(c));
  be32_put(f, 12);
  for (uint8_t v : {0x12, 0x34, 0x56, 0x78}) f.push_back(v);
  const Bank bank = parse_bank(f);
  expect(bank.name == "Test" && bank.waves.size() == 1 && bank.waves[0].name == "beep" && bank.waves[0].format == 3 &&
             bank.waves[0].sample_rate == 48000 && bank.waves[0].channels == 1 && bank.waves[0].gain == 1.0f &&
             bank.waves[0].data.size() == 4 && bank.waves[0].data[0] == 0x12,
         "bank parse");
  const Bytes x = xma1_riff(bank.waves[0]);
  expect(x.size() == 12 + 8 + 32 + 8 + 4 && x[20] == 0x65 && x[21] == 0x01 && x[49] == 1, "xma1 riff header");
}

int main() {
  test_bank();
  test_sha1();
  test_aes();
  test_lzx_uncompressed();
  std::printf(failures ? "%d failure(s)\n" : "all tests passed\n", failures);
  return failures ? 1 : 0;
}

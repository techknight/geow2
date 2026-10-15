#include "extract/aes128.h"

#include <array>
#include <cstring>

namespace geow2 {
namespace {

uint8_t xtime(uint8_t x) { return uint8_t((x << 1) ^ ((x & 0x80) ? 0x1B : 0)); }

uint8_t gmul(uint8_t a, uint8_t b) {
  uint8_t r = 0;
  while (b) {
    if (b & 1) r ^= a;
    a = xtime(a);
    b >>= 1;
  }
  return r;
}

struct Tables {
  std::array<uint8_t, 256> sbox{}, inv{};
  Tables() {
    // S-box from FIPS-197 5.1.1: multiplicative inverse in GF(2^8), then the affine map.
    for (int i = 0; i < 256; ++i) {
      uint8_t x = 0;
      if (i) {
        for (int c = 1; c < 256; ++c)
          if (gmul(uint8_t(i), uint8_t(c)) == 1) { x = uint8_t(c); break; }
      }
      uint8_t s = x;
      for (int k = 1; k <= 4; ++k) s ^= uint8_t((x << k) | (x >> (8 - k)));
      s ^= 0x63;
      sbox[i] = s;
      inv[s] = uint8_t(i);
    }
  }
};

const Tables& tables() {
  static const Tables t;
  return t;
}

}  // namespace

Aes128Decrypt::Aes128Decrypt(const uint8_t key[16]) {
  const auto& t = tables();
  std::memcpy(round_keys_, key, 16);
  uint8_t rcon = 1;
  for (int i = 16; i < 176; i += 4) {
    uint8_t w[4];
    std::memcpy(w, round_keys_ + i - 4, 4);
    if (i % 16 == 0) {
      uint8_t tmp = w[0];
      w[0] = uint8_t(t.sbox[w[1]] ^ rcon);
      w[1] = t.sbox[w[2]];
      w[2] = t.sbox[w[3]];
      w[3] = t.sbox[tmp];
      rcon = xtime(rcon);
    }
    for (int k = 0; k < 4; ++k) round_keys_[i + k] = round_keys_[i - 16 + k] ^ w[k];
  }
}

void Aes128Decrypt::decrypt_block(const uint8_t in[16], uint8_t out[16]) const {
  const auto& t = tables();
  uint8_t s[16];
  for (int i = 0; i < 16; ++i) s[i] = in[i] ^ round_keys_[160 + i];
  for (int round = 9; round >= 0; --round) {
    // InvShiftRows (state is column-major: s[col * 4 + row]).
    uint8_t r[16];
    for (int c = 0; c < 4; ++c)
      for (int row = 0; row < 4; ++row) r[((c + row) % 4) * 4 + row] = s[c * 4 + row];
    // InvSubBytes, AddRoundKey.
    for (int i = 0; i < 16; ++i) s[i] = t.inv[r[i]] ^ round_keys_[round * 16 + i];
    if (round == 0) break;
    // InvMixColumns.
    for (int c = 0; c < 4; ++c) {
      uint8_t a0 = s[c * 4], a1 = s[c * 4 + 1], a2 = s[c * 4 + 2], a3 = s[c * 4 + 3];
      s[c * 4 + 0] = gmul(a0, 14) ^ gmul(a1, 11) ^ gmul(a2, 13) ^ gmul(a3, 9);
      s[c * 4 + 1] = gmul(a0, 9) ^ gmul(a1, 14) ^ gmul(a2, 11) ^ gmul(a3, 13);
      s[c * 4 + 2] = gmul(a0, 13) ^ gmul(a1, 9) ^ gmul(a2, 14) ^ gmul(a3, 11);
      s[c * 4 + 3] = gmul(a0, 11) ^ gmul(a1, 13) ^ gmul(a2, 9) ^ gmul(a3, 14);
    }
  }
  std::memcpy(out, s, 16);
}

void Aes128Decrypt::decrypt_cbc(uint8_t* data, size_t len, uint8_t iv[16]) const {
  uint8_t prev[16], plain[16];
  for (size_t off = 0; off + 16 <= len; off += 16) {
    std::memcpy(prev, data + off, 16);
    decrypt_block(data + off, plain);
    for (int i = 0; i < 16; ++i) data[off + i] = plain[i] ^ iv[i];
    std::memcpy(iv, prev, 16);
  }
}

}  // namespace geow2

#include "extract/sha1.h"

#include <cstring>

namespace geow2 {
namespace {
uint32_t rol(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }
}  // namespace

Sha1::Sha1() : h_{0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0} {}

void Sha1::block(const uint8_t* p) {
  uint32_t w[80];
  for (int i = 0; i < 16; ++i)
    w[i] = (uint32_t(p[i * 4]) << 24) | (uint32_t(p[i * 4 + 1]) << 16) | (uint32_t(p[i * 4 + 2]) << 8) | p[i * 4 + 3];
  for (int i = 16; i < 80; ++i) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
  uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4];
  for (int i = 0; i < 80; ++i) {
    uint32_t f, k;
    if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
    else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
    else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
    else { f = b ^ c ^ d; k = 0xCA62C1D6; }
    uint32_t t = rol(a, 5) + f + e + k + w[i];
    e = d; d = c; c = rol(b, 30); b = a; a = t;
  }
  h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d; h_[4] += e;
}

void Sha1::update(const uint8_t* data, size_t len) {
  total_ += len;
  while (len) {
    size_t n = 64 - buf_len_;
    if (n > len) n = len;
    std::memcpy(buf_ + buf_len_, data, n);
    buf_len_ += n;
    data += n;
    len -= n;
    if (buf_len_ == 64) {
      block(buf_);
      buf_len_ = 0;
    }
  }
}

std::array<uint8_t, 20> Sha1::finish() {
  uint64_t bits = total_ * 8;
  uint8_t pad = 0x80;
  update(&pad, 1);
  uint8_t zero = 0;
  while (buf_len_ != 56) update(&zero, 1);
  uint8_t len[8];
  for (int i = 0; i < 8; ++i) len[i] = uint8_t(bits >> (56 - i * 8));
  update(len, 8);
  std::array<uint8_t, 20> out;
  for (int i = 0; i < 5; ++i)
    for (int k = 0; k < 4; ++k) out[i * 4 + k] = uint8_t(h_[i] >> (24 - k * 8));
  return out;
}

std::array<uint8_t, 20> sha1(std::span<const uint8_t> data) {
  Sha1 s;
  s.update(data);
  return s.finish();
}

std::string sha1_hex(std::span<const uint8_t> data) {
  static const char* hex = "0123456789ABCDEF";
  std::string s;
  for (uint8_t b : sha1(data)) {
    s += hex[b >> 4];
    s += hex[b & 15];
  }
  return s;
}

}  // namespace geow2

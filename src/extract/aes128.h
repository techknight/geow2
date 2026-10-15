#pragma once

#include <cstddef>
#include <cstdint>

namespace geow2 {

// AES-128 decryption only: the XEX loader needs ECB for the file key and
// CBC (zero IV) for the image.
class Aes128Decrypt {
 public:
  explicit Aes128Decrypt(const uint8_t key[16]);
  void decrypt_block(const uint8_t in[16], uint8_t out[16]) const;
  // Decrypts len bytes (a multiple of 16) in place; iv is updated so calls chain.
  void decrypt_cbc(uint8_t* data, size_t len, uint8_t iv[16]) const;

 private:
  uint8_t round_keys_[176];
};

}  // namespace geow2

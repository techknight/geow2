#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace geow2 {

class Sha1 {
 public:
  Sha1();
  void update(const uint8_t* data, size_t len);
  void update(std::span<const uint8_t> data) { update(data.data(), data.size()); }
  std::array<uint8_t, 20> finish();

 private:
  void block(const uint8_t* p);
  uint32_t h_[5];
  uint8_t buf_[64];
  size_t buf_len_ = 0;
  uint64_t total_ = 0;
};

std::array<uint8_t, 20> sha1(std::span<const uint8_t> data);
std::string sha1_hex(std::span<const uint8_t> data);  // upper case

}  // namespace geow2

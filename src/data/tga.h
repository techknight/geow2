#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace geow2 {

// A decoded image: rows top to bottom, RGBA8.
struct Image {
  uint32_t width = 0, height = 0;
  std::vector<uint8_t> rgba;
};

// A Targa file as the game's D3DX decoder reads it (sub_820BA060): true
// colour (type 2) or run-length true colour (type 10), 24 or 32 bits per
// pixel, stored bottom-up unless the descriptor's bit 5 says top-down and
// right-to-left when bit 4 is set; 24-bit pixels get alpha 255. Null for
// anything else.
std::optional<Image> decode_tga(std::span<const uint8_t> file);

}  // namespace geow2

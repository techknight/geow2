#pragma once

#include <optional>
#include <span>

#include "data/tga.h"

namespace geow2 {

// A PNG image (8-bit grey, grey + alpha, RGB, RGBA or palette, not
// interlaced) to RGBA8, top row first. Null for anything else.
std::optional<Image> decode_png(std::span<const uint8_t> file);

// zlib (RFC 1950/1951) decompression; null on a malformed stream.
std::optional<std::vector<uint8_t>> inflate_zlib(std::span<const uint8_t> data);

}  // namespace geow2

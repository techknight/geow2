#pragma once

#include <cstdint>
#include <filesystem>
#include <span>

namespace geow2 {

// Writes 8-bit RGBA pixels (rows top to bottom) as a PNG with stored
// (uncompressed) deflate blocks: no dependency, larger files.
void write_png(const std::filesystem::path& path, uint32_t width, uint32_t height, std::span<const uint8_t> rgba);

}  // namespace geow2

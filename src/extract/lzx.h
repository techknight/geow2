#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "extract/bytes.h"

namespace geow2 {

// Decodes an LZX stream (the format of Microsoft's CAB/LZX specification,
// no delta extensions) with the given window size (2^15 .. 2^21), never
// resetting, into exactly out_size bytes. The input is the concatenation of
// the compressed frames; each frame decodes to 32768 bytes.
Bytes lzx_decompress(std::span<const uint8_t> in, uint32_t window_size, size_t out_size);

}  // namespace geow2

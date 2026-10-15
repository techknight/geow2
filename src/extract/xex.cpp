#include "extract/xex.h"

#include <cstring>

#include "extract/aes128.h"
#include "extract/lzx.h"
#include "extract/sha1.h"

namespace geow2 {
namespace {

// The Xbox 360 retail XEX key (public; the same value every XEX loader uses).
constexpr uint8_t kRetailKey[16] = {0x20, 0xB1, 0x85, 0xA5, 0x9D, 0x28, 0xFD, 0xC3,
                                    0x40, 0x58, 0x3F, 0xBB, 0x08, 0x96, 0xBF, 0x91};

constexpr uint32_t kKeyFileFormat = 0x000003FF;
constexpr uint32_t kKeyEntryPoint = 0x00010100;
constexpr uint32_t kKeyImportLibraries = 0x000103FF;
constexpr uint32_t kKeyOriginalPeName = 0x000183FF;

constexpr uint32_t kImageFlagSmallPages = 0x10000000;

struct OptionalHeader {
  uint32_t key;
  uint32_t value;
};

bool digest_equal(const std::array<uint8_t, 20>& a, const uint8_t* b) { return std::memcmp(a.data(), b, 20) == 0; }

}  // namespace

XexImage xex_load(std::span<const uint8_t> x) {
  need(x, 0, 0x18, "xex header");
  if (std::memcmp(x.data(), "XEX2", 4)) throw FormatError("xex: not an XEX2 file");
  const uint32_t pe_offset = be32(&x[0x08]);
  const uint32_t security_offset = be32(&x[0x10]);
  const uint32_t header_count = be32(&x[0x14]);
  need(x, 0x18, size_t(header_count) * 8, "xex optional headers");

  std::vector<OptionalHeader> headers;
  for (uint32_t i = 0; i < header_count; ++i) headers.push_back({be32(&x[0x18 + i * 8]), be32(&x[0x1C + i * 8])});
  auto find = [&](uint32_t key) -> const OptionalHeader* {
    for (const auto& h : headers)
      if (h.key == key) return &h;
    return nullptr;
  };

  XexImage img;

  // Security info.
  need(x, security_offset, 0x184, "xex security info");
  const uint8_t* sec = &x[security_offset];
  const uint32_t image_size = be32(sec + 0x004);
  img.image_flags = be32(sec + 0x10C);
  img.load_address = be32(sec + 0x110);
  const uint8_t* file_key = sec + 0x150;
  const uint32_t page_count = be32(sec + 0x180);
  need(x, security_offset + 0x184, size_t(page_count) * 24, "xex page descriptors");

  if (auto* h = find(kKeyEntryPoint)) img.entry_point = h->value;
  if (auto* h = find(kKeyOriginalPeName)) {
    need(x, h->value, 4, "xex original name");
    const uint32_t n = be32(&x[h->value]);
    need(x, h->value + 4, n, "xex original name");
    img.original_pe_name.assign(reinterpret_cast<const char*>(&x[h->value + 4]), n);
    img.original_pe_name = img.original_pe_name.c_str();
  }

  // File format: encryption and compression.
  const OptionalHeader* ff = find(kKeyFileFormat);
  if (!ff) throw FormatError("xex: no file format header");
  need(x, ff->value, 8, "xex file format");
  const uint8_t* ffi = &x[ff->value];
  const uint32_t ffi_size = be32(ffi);
  need(x, ff->value, ffi_size, "xex file format");
  const uint16_t encryption = be16(ffi + 4);
  const uint16_t compression = be16(ffi + 6);

  Bytes data(x.begin() + std::ptrdiff_t(pe_offset), x.end());
  if (encryption == 1) {
    uint8_t session_key[16];
    Aes128Decrypt(kRetailKey).decrypt_block(file_key, session_key);
    uint8_t iv[16] = {};
    Aes128Decrypt(session_key).decrypt_cbc(data.data(), data.size() & ~size_t(15), iv);
  } else if (encryption != 0) {
    throw FormatError("xex: unknown encryption type");
  }

  if (compression == 0) {
    img.image = std::move(data);
  } else if (compression == 1) {
    // Basic: runs of stored data, each followed by zero fill.
    const uint32_t blocks = (ffi_size - 8) / 8;
    size_t src = 0;
    for (uint32_t i = 0; i < blocks; ++i) {
      const uint32_t n = be32(ffi + 8 + i * 8);
      const uint32_t zeros = be32(ffi + 12 + i * 8);
      need(data, src, n, "xex basic block");
      img.image.insert(img.image.end(), data.begin() + std::ptrdiff_t(src), data.begin() + std::ptrdiff_t(src + n));
      img.image.insert(img.image.end(), zeros, 0);
      src += n;
    }
  } else if (compression == 2) {
    // Normal: a chain of hashed blocks, each holding the next block's size
    // and hash and then 16-bit-length-prefixed LZX frames.
    need(x, ff->value, 0x24, "xex compression info");
    const uint32_t window = be32(ffi + 8);
    uint32_t block_size = be32(ffi + 12);
    uint8_t block_hash[20];
    std::memcpy(block_hash, ffi + 16, 20);
    Bytes lzx;
    size_t p = 0;
    while (block_size) {
      need(data, p, block_size, "xex compressed block");
      const uint8_t* b = &data[p];
      if (!digest_equal(sha1({b, block_size}), block_hash))
        throw FormatError("xex: compressed block hash mismatch (wrong key or damaged file)");
      const uint32_t next_size = be32(b);
      std::memcpy(block_hash, b + 4, 20);
      size_t q = 24;
      while (q + 2 <= block_size) {
        const uint16_t n = be16(b + q);
        q += 2;
        if (n == 0) break;
        if (q + n > block_size) throw FormatError("xex: LZX frame overruns block");
        lzx.insert(lzx.end(), b + q, b + q + n);
        q += n;
      }
      p += block_size;
      block_size = next_size;
    }
    img.image = lzx_decompress(lzx, window, image_size);
  } else {
    throw FormatError("xex: unsupported compression type");
  }
  if (img.image.size() < image_size) img.image.resize(image_size, 0);
  if (img.image.size() > image_size) img.image.resize(image_size);

  // Page digests form a chain: the security info's digest at +0x114 is the
  // SHA-1 of the first page run followed by its descriptor (value and
  // digest), and each descriptor's digest covers the next run and descriptor
  // the same way.
  const uint32_t page_size = (img.image_flags & kImageFlagSmallPages) ? 0x1000 : 0x10000;
  const uint8_t* desc = sec + 0x184;
  const uint8_t* expected = sec + 0x114;
  uint32_t addr = 0;
  for (uint32_t i = 0; i < page_count; ++i) {
    const uint32_t v = be32(desc + i * 24);
    const uint32_t size = (v >> 4) * page_size;
    need(img.image, addr, size, "xex page run");
    Sha1 h;
    h.update(&img.image[addr], size);
    h.update(desc + i * 24, 24);
    if (!digest_equal(h.finish(), expected))
      throw FormatError("xex: page digest mismatch at image offset " + std::to_string(addr));
    expected = desc + i * 24 + 4;
    img.page_runs.push_back({img.load_address + addr, size, v & 0xF});
    addr += size;
  }

  // Imports.
  if (auto* h = find(kKeyImportLibraries)) {
    need(x, h->value, 12, "xex imports");
    const uint8_t* ih = &x[h->value];
    const uint32_t strings_size = be32(ih + 4);
    const uint32_t lib_count = be32(ih + 8);
    std::vector<std::string> names;
    for (size_t s = 0; s < strings_size;) {
      std::string n(reinterpret_cast<const char*>(ih + 12 + s));
      if (n.empty()) break;
      names.push_back(n);
      s += (n.size() + 4) & ~size_t(3);
    }
    const uint8_t* lib = ih + 12 + strings_size;
    for (uint32_t l = 0; l < lib_count; ++l) {
      const uint32_t lib_size = be32(lib);
      const uint16_t name_index = be16(lib + 0x24);
      const uint16_t records = be16(lib + 0x26);
      const std::string libname = name_index < names.size() ? names[name_index] : "?";
      for (uint16_t r = 0; r < records; ++r) {
        const uint32_t ra = be32(lib + 0x28 + r * 4);
        if (ra < img.load_address || ra - img.load_address + 4 > img.image.size()) continue;
        const uint32_t rv = be32(&img.image[ra - img.load_address]);
        img.imports.push_back({libname, ra, rv & 0xFFFF, (rv >> 24) == 1});
      }
      lib += lib_size;
    }
  }
  return img;
}

}  // namespace geow2

#include "extract/lzx.h"

#include <algorithm>
#include <array>
#include <vector>

namespace geow2 {
namespace {

constexpr int kNumChars = 256;
constexpr int kPretreeSize = 20;
constexpr int kLengthTreeSize = 249;
constexpr int kAlignedSize = 8;
constexpr int kMinMatch = 2;
constexpr int kNumPrimaryLengths = 7;
constexpr int kMaxBits = 16;
constexpr size_t kFrameSize = 32768;

enum BlockType { kVerbatim = 1, kAligned = 2, kUncompressed = 3 };

// 16-bit little-endian words, consumed most significant bit first.
class BitReader {
 public:
  explicit BitReader(std::span<const uint8_t> in) : in_(in) {}

  uint32_t read(int n) {
    if (n == 0) return 0;
    fill();
    uint32_t v = uint32_t(buf_ >> (64 - n));
    buf_ <<= n;
    left_ -= n;
    return v;
  }
  uint32_t bit() { return read(1); }

  // Drops the bits left in the current 16-bit word.
  void align16() {
    int drop = left_ & 15;
    buf_ <<= drop;
    left_ -= drop;
  }

  // Uncompressed-block entry: drop to the next word boundary (a whole word if
  // already aligned), then hand the byte position to the caller.
  size_t byte_position_for_raw() {
    fill();
    int drop = left_ & 15;
    if (drop == 0) drop = 16;
    buf_ <<= drop;
    left_ -= drop;
    size_t p = pos_ - size_t(left_ / 8);
    buf_ = 0;
    left_ = 0;
    pos_ = p;
    return p;
  }
  void set_byte_position(size_t p) {
    buf_ = 0;
    left_ = 0;
    pos_ = p;
  }
  size_t raw_position() const { return pos_; }

 private:
  void fill() {
    while (left_ <= 48) {
      uint32_t w = 0;
      if (pos_ + 1 < in_.size()) w = le16(in_.data() + pos_);
      else if (pos_ < in_.size()) w = in_[pos_];
      pos_ += 2;
      buf_ |= uint64_t(w) << (48 - left_);
      left_ += 16;
    }
  }

  std::span<const uint8_t> in_;
  size_t pos_ = 0;
  uint64_t buf_ = 0;
  int left_ = 0;
};

// Canonical Huffman decoder (codes assigned by length, then symbol order).
class Huffman {
 public:
  void build(const uint8_t* lens, int n) {
    count_.fill(0);
    symbols_.clear();
    for (int i = 0; i < n; ++i) count_[lens[i]]++;
    count_[0] = 0;
    for (int len = 1; len <= kMaxBits; ++len)
      for (int i = 0; i < n; ++i)
        if (lens[i] == len) symbols_.push_back(uint16_t(i));
  }

  int decode(BitReader& br) const {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= kMaxBits; ++len) {
      code |= int(br.bit());
      int count = count_[len];
      if (code - count < first) return symbols_[size_t(index + (code - first))];
      index += count;
      first += count;
      first <<= 1;
      code <<= 1;
    }
    throw FormatError("lzx: bad Huffman code");
  }

 private:
  std::array<int, kMaxBits + 1> count_{};
  std::vector<uint16_t> symbols_;
};

int position_slots(uint32_t window_bits) {
  switch (window_bits) {
    case 15: return 30;
    case 16: return 32;
    case 17: return 34;
    case 18: return 36;
    case 19: return 38;
    case 20: return 42;
    case 21: return 50;
  }
  throw FormatError("lzx: unsupported window size");
}

struct Tables {
  std::array<uint8_t, 51> extra_bits{};
  std::array<uint32_t, 51> position_base{};
  Tables() {
    for (int i = 0, j = 0; i < 50; i += 2) {
      extra_bits[size_t(i)] = uint8_t(j);
      extra_bits[size_t(i + 1)] = uint8_t(j);
      if (i != 0 && j < 17) ++j;
    }
    uint32_t base = 0;
    for (size_t i = 0; i < 51; ++i) {
      position_base[i] = base;
      base += 1u << extra_bits[i];
    }
  }
};

// Reads code lengths [first, last) as deltas from the previous ones, through a pretree.
void read_lengths(BitReader& br, uint8_t* lens, int first, int last) {
  uint8_t pre_lens[kPretreeSize];
  for (auto& l : pre_lens) l = uint8_t(br.read(4));
  Huffman pre;
  pre.build(pre_lens, kPretreeSize);
  for (int x = first; x < last;) {
    int z = pre.decode(br);
    if (z == 17) {
      int y = 4 + int(br.read(4));
      while (y-- && x < last) lens[x++] = 0;
    } else if (z == 18) {
      int y = 20 + int(br.read(5));
      while (y-- && x < last) lens[x++] = 0;
    } else if (z == 19) {
      int y = 4 + int(br.read(1));
      z = pre.decode(br);
      int v = lens[x] - z;
      if (v < 0) v += 17;
      while (y-- && x < last) lens[x++] = uint8_t(v);
    } else {
      int v = lens[x] - z;
      if (v < 0) v += 17;
      lens[x++] = uint8_t(v);
    }
  }
}

}  // namespace

Bytes lzx_decompress(std::span<const uint8_t> in, uint32_t window_size, size_t out_size) {
  static const Tables tab;
  uint32_t window_bits = 0;
  while ((1u << window_bits) < window_size) ++window_bits;
  if ((1u << window_bits) != window_size) throw FormatError("lzx: window size not a power of two");
  const int slots = position_slots(window_bits);
  const int main_size = kNumChars + slots * 8;

  Bytes out(out_size);
  std::vector<uint8_t> main_lens(size_t(main_size), 0), length_lens(kLengthTreeSize, 0);
  uint8_t aligned_lens[kAlignedSize] = {};
  Huffman main_tree, length_tree, aligned_tree;

  BitReader br(in);
  uint32_t r0 = 1, r1 = 1, r2 = 1;
  size_t pos = 0;
  size_t frame_start = 0;
  size_t frame_end = std::min(kFrameSize, out_size);

  // Intel E8 call translation header.
  uint32_t intel_size = 0;
  if (br.bit()) {
    uint32_t hi = br.read(16);
    uint32_t lo = br.read(16);
    intel_size = (hi << 16) | lo;
  }
  bool intel_started = false;

  int block_type = 0;
  size_t block_remaining = 0, block_length = 0;

  auto finish_frames = [&]() {
    while (pos >= frame_end && frame_end <= out_size) {
      // E8 translation (only within the first 32768 frames, as the format defines).
      if (intel_started && intel_size && (frame_start / kFrameSize) < 32768 && frame_end - frame_start > 10) {
        uint8_t* data = out.data() + frame_start;
        size_t n = frame_end - frame_start;
        int32_t cur = int32_t(frame_start);
        for (size_t i = 0; i < n - 10;) {
          if (data[i] != 0xE8) {
            ++i;
            ++cur;
            continue;
          }
          int32_t abs_off = int32_t(data[i + 1] | (data[i + 2] << 8) | (data[i + 3] << 16) | (uint32_t(data[i + 4]) << 24));
          if (abs_off >= -cur && abs_off < int32_t(intel_size)) {
            int32_t rel = abs_off >= 0 ? abs_off - cur : abs_off + int32_t(intel_size);
            data[i + 1] = uint8_t(rel);
            data[i + 2] = uint8_t(rel >> 8);
            data[i + 3] = uint8_t(rel >> 16);
            data[i + 4] = uint8_t(uint32_t(rel) >> 24);
          }
          i += 5;
          cur += 5;
        }
      }
      br.align16();
      if (frame_end == out_size) {
        frame_end = out_size + 1;
        break;
      }
      frame_start = frame_end;
      frame_end = std::min(frame_end + kFrameSize, out_size);
    }
  };

  while (pos < out_size) {
    if (block_remaining == 0) {
      if (block_type == kUncompressed) {
        // Resume the bitstream after the raw bytes (and the pad byte for odd lengths).
        size_t p = br.raw_position();
        if (block_length & 1) ++p;
        br.set_byte_position(p);
      }
      block_type = int(br.read(3));
      uint32_t hi = br.read(16);
      uint32_t lo = br.read(8);
      block_remaining = block_length = (size_t(hi) << 8) | lo;
      if (block_length == 0) throw FormatError("lzx: empty block");
      switch (block_type) {
        case kAligned:
          for (auto& l : aligned_lens) l = uint8_t(br.read(3));
          aligned_tree.build(aligned_lens, kAlignedSize);
          [[fallthrough]];
        case kVerbatim:
          read_lengths(br, main_lens.data(), 0, kNumChars);
          read_lengths(br, main_lens.data(), kNumChars, main_size);
          main_tree.build(main_lens.data(), main_size);
          if (main_lens[0xE8]) intel_started = true;
          read_lengths(br, length_lens.data(), 0, kLengthTreeSize);
          length_tree.build(length_lens.data(), kLengthTreeSize);
          break;
        case kUncompressed: {
          intel_started = true;
          size_t p = br.byte_position_for_raw();
          if (p + 12 > in.size()) throw FormatError("lzx: truncated uncompressed block");
          auto rd32 = [&](size_t o) { return uint32_t(in[o] | (in[o + 1] << 8) | (in[o + 2] << 16) | (uint32_t(in[o + 3]) << 24)); };
          r0 = rd32(p);
          r1 = rd32(p + 4);
          r2 = rd32(p + 8);
          br.set_byte_position(p + 12);
          break;
        }
        default:
          throw FormatError("lzx: bad block type");
      }
    }

    if (block_type == kUncompressed) {
      size_t n = std::min({block_remaining, frame_end - pos, out_size - pos});
      size_t p = br.raw_position();
      if (p + n > in.size()) throw FormatError("lzx: truncated raw data");
      std::copy_n(in.begin() + std::ptrdiff_t(p), n, out.begin() + std::ptrdiff_t(pos));
      br.set_byte_position(p + n);
      pos += n;
      block_remaining -= n;
      finish_frames();
      continue;
    }

    // Verbatim or aligned: decode symbols until the block or the frame ends.
    while (block_remaining > 0 && pos < frame_end && pos < out_size) {
      int sym = main_tree.decode(br);
      if (sym < kNumChars) {
        out[pos++] = uint8_t(sym);
        --block_remaining;
        continue;
      }
      sym -= kNumChars;
      uint32_t match_len = uint32_t(sym & kNumPrimaryLengths);
      if (match_len == kNumPrimaryLengths) match_len += uint32_t(length_tree.decode(br));
      match_len += kMinMatch;
      uint32_t slot = uint32_t(sym >> 3);
      uint32_t offset;
      if (slot > 2) {
        uint32_t extra = slot >= 36 ? 17 : tab.extra_bits[slot];
        offset = tab.position_base[slot] - 2;
        if (block_type == kAligned) {
          if (extra > 3) {
            offset += br.read(int(extra - 3)) << 3;
            offset += uint32_t(aligned_tree.decode(br));
          } else if (extra == 3) {
            offset += uint32_t(aligned_tree.decode(br));
          } else if (extra > 0) {
            offset += br.read(int(extra));
          } else {
            offset = 1;
          }
        } else {
          offset += br.read(int(extra));
        }
        r2 = r1;
        r1 = r0;
        r0 = offset;
      } else if (slot == 0) {
        offset = r0;
      } else if (slot == 1) {
        offset = r1;
        r1 = r0;
        r0 = offset;
      } else {
        offset = r2;
        r2 = r0;
        r0 = offset;
      }
      if (offset == 0 || offset > pos) throw FormatError("lzx: match before start of output");
      if (match_len > block_remaining) throw FormatError("lzx: match overruns block");
      for (uint32_t i = 0; i < match_len && pos < out_size; ++i, ++pos) out[pos] = out[pos - offset];
      block_remaining -= match_len;
    }
    finish_frames();
  }
  return out;
}

}  // namespace geow2

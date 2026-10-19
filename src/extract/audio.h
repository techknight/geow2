#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "extract/bytes.h"
#include "extract/hooks.h"

namespace geow2 {

// One wave of a `.baf` bank (format: docs/formats/baf.md; the game's reader
// is sub_820FEB10 / sub_820FE8C0).
struct BankWave {
  std::string name;
  uint32_t format = 0;  // 3: 16-bit big-endian PCM, 8: XMA1 packets
  uint32_t sample_rate = 0, bits = 0, channels = 0;
  float gain = 1.0f;    // +0x2C
  uint32_t flags = 0;   // +0x30
  std::span<const uint8_t> data;
};

struct Bank {
  std::string name;
  uint8_t kind = 0;
  std::vector<BankWave> waves;
};

// Throws FormatError on anything the game's reader would reject.
Bank parse_bank(std::span<const uint8_t> file);

// A RIFF WAVE file of 16-bit little-endian PCM.
Bytes wav_pcm16(uint32_t sample_rate, uint32_t channels, std::span<const uint8_t> le_samples);

// The XMA1 packets of a wave in a RIFF container (format tag 0x0165, one
// stream) that FFmpeg's xma1 decoder reads.
Bytes xma1_riff(const BankWave& w);

// Whether `ffmpeg` (the executable named by GEOW2_FFMPEG, else the one on
// PATH) runs.
bool ffmpeg_available(const ExtractHooks& hooks = {});

// Writes every wave of every bank in `audio_dir` as `<out>/<bank>/<wave>.wav`
// (16-bit PCM) and `<out>/<bank>/index.txt`. XMA waves go through `ffmpeg`;
// without it they are skipped with a message. Progress runs from `from` to
// `to` over the waves. Returns the waves written.
size_t extract_audio(const std::filesystem::path& audio_dir, const std::filesystem::path& out,
                     const ExtractHooks& hooks = {}, double from = 0.0, double to = 1.0);

}  // namespace geow2

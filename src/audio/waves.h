#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace geow2::audio {

// One wave of a bank, decoded to 16-bit PCM by geow2-extract.
struct Wave {
  std::string name;
  uint32_t format = 3;  // the bank's format: 3 PCM, 8 XMA (decoded by the extractor)
  uint32_t rate = 48000;
  uint32_t channels = 1;
  std::vector<int16_t> samples;  // interleaved
  size_t frames() const { return channels ? samples.size() / channels : 0; }
};

struct WaveBank {
  std::string name;
  std::vector<Wave> waves;  // in bank order (index.txt)
};

// The banks in the data folder: `<dir>/<bank>/<wave>.wav` with the
// bank order in `<dir>/<bank>/index.txt`.
class WaveStore {
 public:
  // Loads every bank under `dir`; returns the number of waves loaded. Waves
  // the extractor could not write are missing (and their sources silent).
  size_t load(const std::filesystem::path& dir);
  void add(WaveBank bank) { banks_.push_back(std::move(bank)); }

  // The original's lookup (sub_820EE5A0): bank and wave names compare
  // case-insensitively over at most 32 characters (strnicmp), first match
  // in bank order. Null when either is missing.
  const Wave* find(std::string_view bank, std::string_view wave) const;

 private:
  std::vector<WaveBank> banks_;
};

// A RIFF WAVE file of 16-bit PCM; throws FormatError on anything else.
Wave parse_wav(std::span<const uint8_t> file);

}  // namespace geow2::audio

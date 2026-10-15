#include "audio/waves.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <sstream>

#include "extract/bytes.h"

namespace geow2::audio {

namespace {

uint32_t le32(const uint8_t* p) { return p[0] | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }

// strnicmp(a, b, 32) == 0.
bool same_name(std::string_view a, std::string_view b) {
  for (size_t i = 0; i < 32; ++i) {
    const char ca = i < a.size() ? a[i] : '\0';
    const char cb = i < b.size() ? b[i] : '\0';
    const auto la = char(std::tolower(static_cast<unsigned char>(ca)));
    const auto lb = char(std::tolower(static_cast<unsigned char>(cb)));
    if (la != lb) return false;
    if (ca == '\0') return true;
  }
  return true;
}

}  // namespace

Wave parse_wav(std::span<const uint8_t> f) {
  need(f, 0, 12, "wav header");
  if (std::memcmp(f.data(), "RIFF", 4) != 0 || std::memcmp(f.data() + 8, "WAVE", 4) != 0)
    throw FormatError("wav: not a RIFF WAVE file");
  Wave w;
  bool have_fmt = false, have_data = false;
  size_t at = 12;
  while (at + 8 <= f.size()) {
    const uint32_t size = le32(f.data() + at + 4);
    need(f, at + 8, size, "wav chunk");
    const uint8_t* body = f.data() + at + 8;
    if (std::memcmp(f.data() + at, "fmt ", 4) == 0) {
      if (size < 16) throw FormatError("wav: short fmt chunk");
      if (le16(body) != 1 || le16(body + 14) != 16) throw FormatError("wav: not 16-bit PCM");
      w.channels = le16(body + 2);
      w.rate = le32(body + 4);
      have_fmt = true;
    } else if (std::memcmp(f.data() + at, "data", 4) == 0) {
      w.samples.resize(size / 2);
      for (size_t i = 0; i < w.samples.size(); ++i) w.samples[i] = int16_t(le16(body + 2 * i));
      have_data = true;
    }
    at += 8 + size + (size & 1);
  }
  if (!have_fmt || !have_data || w.channels == 0) throw FormatError("wav: missing fmt or data");
  w.samples.resize(w.samples.size() - w.samples.size() % w.channels);
  return w;
}

size_t WaveStore::load(const std::filesystem::path& dir) {
  namespace fs = std::filesystem;
  size_t loaded = 0;
  std::error_code ec;
  std::vector<fs::path> dirs;
  for (const auto& e : fs::directory_iterator(dir, ec))
    if (e.is_directory()) dirs.push_back(e.path());
  std::sort(dirs.begin(), dirs.end());
  for (const fs::path& d : dirs) {
    std::ifstream index(d / "index.txt");
    if (!index) continue;
    WaveBank bank;
    bank.name = d.filename().string();
    // index.txt lines: "<i> <name> format <f> rate <r> ...", names may hold spaces.
    std::string line;
    while (std::getline(index, line)) {
      const size_t first = line.find(' ');
      const size_t fmt = line.find(" format ");
      if (first == std::string::npos || fmt == std::string::npos || fmt <= first) continue;
      Wave w;
      w.name = line.substr(first + 1, fmt - first - 1);
      const uint32_t format = uint32_t(std::strtoul(line.c_str() + fmt + 8, nullptr, 10));
      const fs::path file = d / (w.name + ".wav");
      if (fs::exists(file)) {
        Wave pcm = parse_wav(read_file(file));
        pcm.name = w.name;
        w = std::move(pcm);
        ++loaded;
      }
      w.format = format;
      bank.waves.push_back(std::move(w));
    }
    banks_.push_back(std::move(bank));
  }
  return loaded;
}

const Wave* WaveStore::find(std::string_view bank, std::string_view wave) const {
  for (const WaveBank& b : banks_) {
    if (!same_name(b.name, bank)) continue;
    for (const Wave& w : b.waves)
      if (same_name(w.name, wave)) return w.samples.empty() ? nullptr : &w;
    return nullptr;
  }
  return nullptr;
}

}  // namespace geow2::audio

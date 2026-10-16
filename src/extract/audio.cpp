#include "extract/audio.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace geow2 {
namespace {

void put16(Bytes& b, uint32_t v) {
  b.push_back(uint8_t(v));
  b.push_back(uint8_t(v >> 8));
}
void put32(Bytes& b, uint32_t v) {
  for (int i = 0; i < 4; ++i) b.push_back(uint8_t(v >> (8 * i)));
}
void put_tag(Bytes& b, const char* tag) { b.insert(b.end(), tag, tag + 4); }

Bytes riff(const Bytes& fmt, std::span<const uint8_t> data) {
  Bytes b;
  put_tag(b, "RIFF");
  put32(b, uint32_t(4 + 8 + fmt.size() + 8 + data.size()));
  put_tag(b, "WAVE");
  put_tag(b, "fmt ");
  put32(b, uint32_t(fmt.size()));
  b.insert(b.end(), fmt.begin(), fmt.end());
  put_tag(b, "data");
  put32(b, uint32_t(data.size()));
  b.insert(b.end(), data.begin(), data.end());
  return b;
}

// One argument for the shell std::system runs: double quotes for cmd.exe
// (Windows paths cannot hold them); single quotes for a POSIX shell, where
// nothing inside them is special but the quote itself.
std::string quote(const std::string& s) {
#ifdef _WIN32
  return "\"" + s + "\"";
#else
  std::string q = "'";
  for (char c : s) q += c == '\'' ? std::string("'\\''") : std::string(1, c);
  return q + "'";
#endif
}
std::string quote(const std::filesystem::path& p) { return quote(p.string()); }

std::string ffmpeg_command() {
  if (const char* env = std::getenv("GEOW2_FFMPEG"); env && *env) return quote(std::string(env));
  return "ffmpeg";
}

bool have_ffmpeg(const std::string& ffmpeg) {
#ifdef _WIN32
  const std::string cmd = "\"" + ffmpeg + " -hide_banner -version >nul 2>&1\"";
#else
  const std::string cmd = ffmpeg + " -hide_banner -version >/dev/null 2>&1";
#endif
  return std::system(cmd.c_str()) == 0;
}

}  // namespace

Bank parse_bank(std::span<const uint8_t> file) {
  // Header (0x34): "BANK", header size 0x34, version 4, wave count, kind
  // byte, name.
  need(file, 0, 0x34, "bank header");
  if (std::memcmp(file.data(), "BANK", 4) != 0) throw FormatError("bank: not a BANK");
  if (be32(file.data() + 4) != 0x34 || be32(file.data() + 8) != 4) throw FormatError("bank: unsupported version");
  const uint32_t count = be32(file.data() + 12);
  if (count == 0) throw FormatError("bank: no waves");
  Bank bank;
  bank.kind = file[0x10];
  bank.name.assign(reinterpret_cast<const char*>(file.data() + 0x11), strnlen(reinterpret_cast<const char*>(file.data() + 0x11), 0x23));
  // Chunks: WAVE records, then DATA payloads (located by the records).
  size_t off = 0x34;
  while (off < file.size()) {
    need(file, off, 8, "bank chunk");
    const uint32_t size = be32(file.data() + off + 4);
    if (size < 8) throw FormatError("bank: bad chunk size");
    need(file, off, size, "bank chunk");
    if (std::memcmp(file.data() + off, "WAVE", 4) == 0) {
      const uint8_t* r = file.data() + off + 8;
      if (size < 8 + 0x34 + 12) throw FormatError("bank: short WAVE");
      BankWave w;
      w.format = be32(r);
      if (w.format != 0 && w.format != 3 && w.format != 7 && w.format != 8 && w.format != 9)
        throw FormatError("bank: unknown wave format");
      w.name.assign(reinterpret_cast<const char*>(r + 4), strnlen(reinterpret_cast<const char*>(r + 4), 32));
      const uint32_t at = be32(r + 0x24), len = be32(r + 0x28);
      need(file, at, len, "bank wave payload");
      w.data = file.subspan(at, len);
      uint32_t gain_bits = be32(r + 0x2C);
      std::memcpy(&w.gain, &gain_bits, 4);
      w.flags = be32(r + 0x30);
      w.sample_rate = be32(r + 0x34);
      w.bits = be32(r + 0x38);
      w.channels = be32(r + 0x3C);
      bank.waves.push_back(std::move(w));
    }
    off += size;
  }
  if (bank.waves.size() != count) throw FormatError("bank: wave count mismatch");
  return bank;
}

Bytes wav_pcm16(uint32_t sample_rate, uint32_t channels, std::span<const uint8_t> le_samples) {
  Bytes fmt;
  put16(fmt, 1);  // PCM
  put16(fmt, channels);
  put32(fmt, sample_rate);
  put32(fmt, sample_rate * channels * 2);
  put16(fmt, channels * 2);
  put16(fmt, 16);
  return riff(fmt, le_samples);
}

Bytes xma1_riff(const BankWave& w) {
  // XMAWAVEFORMAT: tag 0x0165, 16 bits, encode options 0, largest skip 0,
  // one stream, no loops, version 3; the stream: bytes per second, rate,
  // loop start/end 0, subframes 0, channels, channel mask.
  Bytes fmt;
  put16(fmt, 0x0165);
  put16(fmt, 16);
  put16(fmt, 0);
  put16(fmt, 0);
  put16(fmt, 1);
  fmt.push_back(0);
  fmt.push_back(3);
  put32(fmt, w.sample_rate * w.channels * 2);
  put32(fmt, w.sample_rate);
  put32(fmt, 0);
  put32(fmt, 0);
  fmt.push_back(0);
  fmt.push_back(uint8_t(w.channels));
  put16(fmt, w.channels == 2 ? 3 : 0);
  return riff(fmt, w.data);
}

size_t extract_audio(const std::filesystem::path& audio_dir, const std::filesystem::path& out) {
  namespace fs = std::filesystem;
  const std::string ffmpeg = ffmpeg_command();
  const bool decoder = have_ffmpeg(ffmpeg);
  if (!decoder)
    std::printf("audio: ffmpeg not found (put it on PATH or set GEOW2_FFMPEG): XMA waves skipped, the game will be "
                "partly silent\n");
  size_t written = 0, skipped = 0;
  for (const auto& entry : fs::directory_iterator(audio_dir)) {
    if (entry.path().extension() != ".baf") continue;
    const Bytes file = read_file(entry.path());
    const Bank bank = parse_bank(file);
    const fs::path dir = out / entry.path().stem();
    fs::create_directories(dir);
    std::string index;
    for (size_t i = 0; i < bank.waves.size(); ++i) {
      const BankWave& w = bank.waves[i];
      char line[160];
      std::snprintf(line, sizeof line, "%zu %s format %u rate %u channels %u gain %g flags 0x%X\n", i, w.name.c_str(),
                    w.format, w.sample_rate, w.channels, double(w.gain), w.flags);
      index += line;
      const fs::path wav = dir / (w.name + ".wav");
      if (w.format == 3 && w.bits == 16) {
        // Big-endian 16-bit PCM: swapped to little-endian.
        Bytes le(w.data.begin(), w.data.end());
        for (size_t k = 0; k + 1 < le.size(); k += 2) std::swap(le[k], le[k + 1]);
        write_file(wav, wav_pcm16(w.sample_rate, w.channels, le));
        ++written;
      } else if (w.format == 8 && decoder) {
        const fs::path tmp = dir / (w.name + ".xma.tmp");
        write_file(tmp, xma1_riff(w));
        std::string cmd = ffmpeg + " -hide_banner -loglevel error -y -i " + quote(tmp) + " -c:a pcm_s16le " + quote(wav);
#ifdef _WIN32
        cmd = "\"" + cmd + "\"";
#endif
        const int rc = std::system(cmd.c_str());
        fs::remove(tmp);
        if (rc != 0) throw FormatError("audio: ffmpeg failed on " + bank.name + "/" + w.name);
        ++written;
      } else {
        ++skipped;
      }
    }
    write_file(dir / "index.txt", {reinterpret_cast<const uint8_t*>(index.data()), index.size()});
  }
  std::printf("audio: %zu waves written to %s%s\n", written, out.string().c_str(),
              skipped ? (", " + std::to_string(skipped) + " skipped").c_str() : "");
  return written;
}

}  // namespace geow2

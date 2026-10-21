// A mutation fuzzer for the parsers of files the game and the extractor
// read (the player's package, the data folder, the profile folder), for a
// sanitizer build (-fsanitize=address,undefined): no libFuzzer needed.
//
//   fuzz <target> <iterations> <seed>... [--rng <n>]
//
// Each iteration takes a seed file, mutates it (bit flips, interesting
// bytes and big-endian words, inserted, removed and repeated ranges, a cut
// end) and hands it to the target. A parser may refuse its input with an
// exception; a crash, a sanitizer report or a hang is a finding. On a
// crash the input is in fuzz-crash.bin (written before each run).

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <functional>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "audio/aobj.h"
#include "audio/waves.h"
#include "data/flask.h"
#include "data/png_read.h"
#include "data/tga.h"
#include "data/xdbf.h"
#include "data/xtlo.h"
#include "extract/audio.h"
#include "extract/bytes.h"
#include "extract/lzx.h"
#include "extract/stfs.h"
#include "extract/xex.h"
#include "game/score_log.h"
#include "game/sequence.h"
#include "net/json.h"
#include "net/scores.h"

using namespace geow2;

namespace {

using Target = std::function<void(const Bytes&)>;

std::string text(const Bytes& b) { return std::string(b.begin(), b.end()); }

const std::map<std::string, Target>& targets() {
  static const std::map<std::string, Target> t = {
      {"stfs",
       [](const Bytes& b) {
         StfsPackage p(b);
         for (const StfsFile& f : p.files()) (void)p.read(f);
       }},
      {"xex", [](const Bytes& b) { (void)xex_load(b); }},
      {"lzx",
       [](const Bytes& b) {
         // The first bytes choose the window and the output size.
         if (b.size() < 4) return;
         const uint32_t window = 1u << (15 + b[0] % 7);
         const size_t out = (size_t(b[1]) << 8 | b[2]) * 64;
         (void)lzx_decompress(std::span(b).subspan(3), window, out);
       }},
      {"flask",
       [](const Bytes& b) {
         Flask f(b);
         for (uint32_t r = 0; r < f.records().size(); ++r) (void)f.decode(r);
       }},
      {"png", [](const Bytes& b) { (void)decode_png(b); }},
      {"tga", [](const Bytes& b) { (void)decode_tga(b); }},
      {"xdbf", [](const Bytes& b) { (void)Xdbf::parse(b); }},
      {"xtlo",
       [](const Bytes& b) {
         TextTable t(b, "en");
         for (const std::string& id : t.ids()) (void)t.find(id);
       }},
      {"aobj", [](const Bytes& b) { (void)parse_aobj(b); }},
      {"wav", [](const Bytes& b) { (void)parse_wav(b); }},
      {"bank", [](const Bytes& b) { (void)parse_bank(b); }},
      {"json", [](const Bytes& b) { (void)json::parse(text(b)); }},
      {"sync", [](const Bytes& b) { (void)parse_sync(200, text(b)); }},
      {"scorelog", [](const Bytes& b) { (void)ScoreLog::parse(text(b)).serialize(); }},
      {"scoretxt", [](const Bytes& b) { (void)ScoreLog::import_board_file(text(b), "d"); }},
      {"sequence", [](const Bytes& b) { (void)parse_sequence_script(text(b)); }},
  };
  return t;
}

struct Mutator {
  std::mt19937_64 rng;
  explicit Mutator(uint64_t seed) : rng(seed) {}
  size_t below(size_t n) { return n ? size_t(rng() % n) : 0; }

  void once(Bytes& b) {
    static const uint32_t kWords[] = {0, 1, 2, 0x7F, 0x80, 0xFF, 0x100, 0x7FFF, 0x8000, 0xFFFF, 0x10000, 0x7FFFFFFF,
                                      0x80000000u, 0xFFFFFFFFu, 0xFFFFFFFEu};
    switch (below(b.empty() ? 1 : 7)) {
      case 0: {  // insert random bytes
        const size_t at = below(b.size() + 1), n = 1 + below(16);
        Bytes add(n);
        for (auto& x : add) x = uint8_t(rng());
        b.insert(b.begin() + std::ptrdiff_t(at), add.begin(), add.end());
        break;
      }
      case 1:  // flip a bit
        b[below(b.size())] ^= uint8_t(1u << below(8));
        break;
      case 2: {  // an interesting byte
        static const uint8_t kBytes[] = {0, 1, 0x7F, 0x80, 0xFF};
        b[below(b.size())] = kBytes[below(sizeof kBytes)];
        break;
      }
      case 3: {  // an interesting big-endian word (sizes, counts, offsets)
        if (b.size() < 4) break;
        const size_t at = below(b.size() - 3) & ~size_t(rng() % 2 ? 3 : 0);
        const uint32_t w = rng() % 4 ? kWords[below(sizeof kWords / sizeof kWords[0])] : uint32_t(b.size() + below(64) - 32);
        b[at] = uint8_t(w >> 24), b[at + 1] = uint8_t(w >> 16), b[at + 2] = uint8_t(w >> 8), b[at + 3] = uint8_t(w);
        break;
      }
      case 4: {  // remove a range
        const size_t at = below(b.size()), n = 1 + below(std::min<size_t>(b.size() - at, 256));
        b.erase(b.begin() + std::ptrdiff_t(at), b.begin() + std::ptrdiff_t(at + n));
        break;
      }
      case 5: {  // repeat a range somewhere else
        const size_t at = below(b.size()), n = 1 + below(std::min<size_t>(b.size() - at, 64));
        const Bytes chunk(b.begin() + std::ptrdiff_t(at), b.begin() + std::ptrdiff_t(at + n));
        const size_t to = below(b.size() + 1);
        b.insert(b.begin() + std::ptrdiff_t(to), chunk.begin(), chunk.end());
        break;
      }
      case 6:  // cut the end
        b.resize(below(b.size() + 1));
        break;
    }
  }
};

}  // namespace

int main(int argc, char** argv) {
  if (argc < 4 || !targets().count(argv[1])) {
    std::fprintf(stderr, "usage: fuzz <target> <iterations> <seed>... [--rng <n>]\ntargets:");
    for (const auto& [name, _] : targets()) std::fprintf(stderr, " %s", name.c_str());
    std::fprintf(stderr, "\n");
    return 2;
  }
  const Target& target = targets().at(argv[1]);
  const long iterations = std::atol(argv[2]);
  uint64_t seed = 1;
  std::vector<Bytes> seeds;
  for (int i = 3; i < argc; ++i) {
    if (std::strcmp(argv[i], "--rng") == 0 && i + 1 < argc) {
      seed = std::strtoull(argv[++i], nullptr, 10);
      continue;
    }
    seeds.push_back(read_file(argv[i]));
  }
  Mutator m(seed);
  size_t refused = 0;
  // The seeds unchanged first: they must parse.
  for (const Bytes& s : seeds) {
    try {
      target(s);
    } catch (const std::exception& e) {
      std::fprintf(stderr, "a seed is refused: %s\n", e.what());
      return 1;
    }
  }
  for (long it = 0; it < iterations; ++it) {
    Bytes b = seeds[m.below(seeds.size())];
    for (size_t k = 1 + m.below(m.rng() % 8 ? 4 : 32); k > 0; --k) m.once(b);
    write_file("fuzz-crash.bin", b);
    try {
      target(b);
    } catch (const std::bad_alloc&) {
      std::printf("iteration %ld: allocation failed (size %zu)\n", it, b.size());
      ++refused;
    } catch (const std::exception&) {
      ++refused;
    }
  }
  std::printf("%s: %ld runs, %zu refused, no crash\n", argv[1], iterations, refused);
  std::remove("fuzz-crash.bin");
  return 0;
}

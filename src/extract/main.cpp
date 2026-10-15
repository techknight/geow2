// geow2-extract: turns the user's Geometry Wars 2 XBLA package into the
// cache the port reads.
//
//   geow2-extract unpack <package-file | extracted-folder> <cache-dir>
//   geow2-extract verify <cache-dir>

#include <cstdio>
#include <filesystem>
#include <map>
#include <string>

#include "data/xtlo.h"
#include "extract/known_files.h"
#include "extract/sha1.h"
#include "extract/stfs.h"
#include "extract/xex.h"

namespace fs = std::filesystem;
using namespace geow2;

namespace {

const KnownFile* known(const std::string& path) {
  for (const auto& k : kKnownFiles)
    if (path == k.path) return &k;
  return nullptr;
}

// Collects the package's files, keyed by package path, from an STFS file or a folder.
std::map<std::string, Bytes> collect(const fs::path& source) {
  std::map<std::string, Bytes> files;
  if (fs::is_directory(source)) {
    for (const auto& k : kKnownFiles) {
      const auto p = source / k.path;
      if (fs::exists(p)) files[k.path] = read_file(p);
    }
    return files;
  }
  Bytes pkg = read_file(source);
  const std::string pkg_sha = sha1_hex(pkg);
  if (pkg_sha != kKnownPackageSha1)
    std::printf("note: package SHA-1 %s is not the known one; checking the files inside\n", pkg_sha.c_str());
  StfsPackage stfs(std::move(pkg));
  for (const auto& f : stfs.files()) files[f.path] = stfs.read(f);
  return files;
}

int check(const std::map<std::string, Bytes>& files) {
  int bad = 0;
  for (const auto& k : kKnownFiles) {
    auto it = files.find(k.path);
    if (it == files.end()) {
      std::printf("missing   %s\n", k.path);
      ++bad;
    } else if (it->second.size() != k.size || sha1_hex(it->second) != k.sha1) {
      std::printf("mismatch  %s\n", k.path);
      ++bad;
    }
  }
  return bad;
}

void write_xex(const Bytes& xex, const fs::path& dir) {
  XexImage img = xex_load(xex);
  write_file(dir / "image.bin", img.image);

  std::string info;
  char line[256];
  std::snprintf(line, sizeof line, "original_pe_name %s\nload_address 0x%08X\nentry_point 0x%08X\nimage_size 0x%08zX\nimage_flags 0x%08X\n",
                img.original_pe_name.c_str(), img.load_address, img.entry_point, img.image.size(), img.image_flags);
  info += line;
  for (const auto& r : img.page_runs) {
    std::snprintf(line, sizeof line, "pages 0x%08X 0x%08X %s\n", r.address, r.size,
                  r.info == 1 ? "code" : r.info == 2 ? "data" : r.info == 3 ? "rodata" : "other");
    info += line;
  }
  write_file(dir / "info.txt", {reinterpret_cast<const uint8_t*>(info.data()), info.size()});

  std::string imports;
  for (const auto& i : img.imports) {
    std::snprintf(line, sizeof line, "0x%08X %s %u %s\n", i.record_address, i.library.c_str(), i.ordinal,
                  i.is_thunk ? "thunk" : "record");
    imports += line;
  }
  write_file(dir / "imports.txt", {reinterpret_cast<const uint8_t*>(imports.data()), imports.size()});
  std::printf("xex: %s, %zu bytes at 0x%08X, entry 0x%08X, %zu imports, all hashes good\n", img.original_pe_name.c_str(),
              img.image.size(), img.load_address, img.entry_point, img.imports.size());
}

int unpack(const std::string& source, const std::string& cache) {
  auto files = collect(source);
  if (int bad = check(files)) {
    std::printf("%d file(s) missing or different: this is not the supported release (584108FF v0x102)\n", bad);
    return 1;
  }
  const fs::path root = cache;
  for (const auto& [path, data] : files) write_file(root / "files" / path, data);
  write_xex(files.at("default.xex"), root / "xex");
  std::printf("unpacked %zu files to %s\n", files.size(), cache.c_str());
  return 0;
}

// Parses every language file the way the game does and counts IDs that resolve.
int check_text(const fs::path& files) {
  static const char* const kLanguages[][2] = {
      {"en", "english"}, {"de", "german"}, {"fr", "french"}, {"es", "spanish"}, {"it", "italian"}};
  int bad = 0;
  for (const auto& [code, name] : kLanguages) {
    const fs::path path = files / (std::string("Text-") + code + ".loc");
    try {
      TextTable tt(read_file(path), name);
      size_t found = 0;
      for (const auto& id : tt.ids()) found += tt.find(id).has_value();
      std::printf("text %s: %zu IDs, %zu resolve\n", code, tt.ids().size(), found);
      if (found != tt.ids().size()) ++bad;
    } catch (const std::exception& e) {
      std::printf("text %s: %s\n", code, e.what());
      ++bad;
    }
  }
  return bad;
}

int verify(const std::string& cache) {
  const fs::path files = fs::path(cache) / "files";
  int bad = check(collect(files));
  if (!bad) bad += check_text(files);
  std::printf(bad ? "%d problem(s)\n" : "all files good\n", bad);
  return bad ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const std::string cmd = argc > 1 ? argv[1] : "";
    if (cmd == "unpack" && argc == 4) return unpack(argv[2], argv[3]);
    if (cmd == "verify" && argc == 3) return verify(argv[2]);
    std::fprintf(stderr,
                 "usage:\n  geow2-extract unpack <package-file | extracted-folder> <cache-dir>\n"
                 "  geow2-extract verify <cache-dir>\n");
    return 2;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    return 1;
  }
}

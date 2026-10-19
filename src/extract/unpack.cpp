#include "extract/unpack.h"

#include <cctype>
#include <cstdio>
#include <optional>

#include "extract/audio.h"
#include "extract/known_files.h"
#include "extract/sha1.h"
#include "extract/stfs.h"
#include "extract/xex.h"

namespace geow2 {
namespace {

namespace fs = std::filesystem;

std::string lower(std::string s) {
  for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

// The file at `rel` under `root`, names matched without regard to case
// (a folder unpacked by another tool may say data.bin or Audio/).
std::optional<fs::path> find_nocase(const fs::path& root, const fs::path& rel) {
  fs::path cur = root;
  for (const auto& part : rel) {
    if (fs::exists(cur / part)) {
      cur /= part;
      continue;
    }
    std::optional<fs::path> hit;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(cur, ec))
      if (lower(e.path().filename().string()) == lower(part.string())) {
        hit = e.path();
        break;
      }
    if (!hit) return std::nullopt;
    cur = *hit;
  }
  return cur;
}

void write_xex(const Bytes& xex, const fs::path& dir, const ExtractHooks& hooks) {
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
  std::snprintf(line, sizeof line, "xex: %s, %zu bytes at 0x%08X, entry 0x%08X, %zu imports, all hashes good",
                img.original_pe_name.c_str(), img.image.size(), img.load_address, img.entry_point, img.imports.size());
  hooks.say(line);
}

}  // namespace

std::map<std::string, Bytes> collect_package(const fs::path& source, const ExtractHooks& hooks) {
  std::map<std::string, Bytes> files;
  if (fs::is_directory(source)) {
    for (const auto& k : kKnownFiles)
      if (const auto p = find_nocase(source, k.path); p && fs::is_regular_file(*p)) files[k.path] = read_file(*p);
    return files;
  }
  Bytes pkg = read_file(source);
  const std::string pkg_sha = sha1_hex(pkg);
  if (pkg_sha != kKnownPackageSha1)
    hooks.say("note: package SHA-1 " + pkg_sha + " is not the known one; checking the files inside");
  StfsPackage stfs(std::move(pkg));
  for (const auto& f : stfs.files()) files[f.path] = stfs.read(f);
  return files;
}

int check_package(const std::map<std::string, Bytes>& files, const ExtractHooks& hooks) {
  int bad = 0;
  for (const auto& k : kKnownFiles) {
    auto it = files.find(k.path);
    if (it == files.end()) {
      hooks.say(std::string("missing   ") + k.path);
      ++bad;
    } else if (it->second.size() != k.size || sha1_hex(it->second) != k.sha1) {
      hooks.say(std::string("mismatch  ") + k.path);
      ++bad;
    }
  }
  return bad;
}

int unpack(const fs::path& source, const fs::path& data_dir, const ExtractHooks& hooks) {
  // Progress: reading and checking the package to 0.15, the files and the
  // executable to 0.2, then the sound (one ffmpeg run per XMA wave).
  hooks.step(0.0);
  auto files = collect_package(source, hooks);
  hooks.step(0.1);
  if (int bad = check_package(files, hooks)) return bad;
  hooks.step(0.15);
  // Only the known files, by their known names (a package's own names
  // never choose where anything is written).
  for (const auto& k : kKnownFiles) write_file(data_dir / "files" / k.path, files.at(k.path));
  write_xex(files.at("default.xex"), data_dir / "xex", hooks);
  hooks.say("unpacked " + std::to_string(std::size(kKnownFiles)) + " files to " + data_dir.string());
  hooks.step(0.2);
  // The sound banks as PCM (XMA through ffmpeg).
  extract_audio(data_dir / "files" / "audio", data_dir / "audio", hooks, 0.2, 1.0);
  return 0;
}

}  // namespace geow2

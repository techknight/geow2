// geow2-extract: turns the user's Geometry Wars 2 XBLA package into the
// data folder the port reads.
//
//   geow2-extract unpack <package-file | extracted-folder | data-dir> <data-dir>
//   geow2-extract verify <data-dir>
//   geow2-extract audio <data-dir>       (redo the sound only: needs ffmpeg)
//   geow2-extract dump-db <data-dir>     (developer aid: every Data.bin value)

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>
#include <string>

#include "data/flask.h"
#include "data/xtlo.h"
#include "game/model_geometry.h"
#include "extract/audio.h"
#include "extract/unpack.h"
#include "version.h"

namespace fs = std::filesystem;
using namespace geow2;

namespace {

// Every line the library logs, printed.
const ExtractHooks kPrint{[](const std::string& line) { std::printf("%s\n", line.c_str()); }, {}, {}};

int unpack_command(const std::string& source, const std::string& data_dir) {
  if (int bad = unpack(source, data_dir, kPrint)) {
    std::printf("%d file(s) missing or different: this is not the supported release (584108FF v0x102)\n", bad);
    return 1;
  }
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

// Prints a decoded value as flat "path = value" lines (developer aid).
void print_value(const std::string& path, const FlaskValue& v) {
  struct Visitor {
    const std::string& path;
    void operator()(std::monostate) const { std::printf("%s = null\n", path.c_str()); }
    void operator()(bool b) const { std::printf("%s = %s\n", path.c_str(), b ? "true" : "false"); }
    void operator()(int64_t i) const { std::printf("%s = %lld\n", path.c_str(), (long long)i); }
    void operator()(uint64_t u) const { std::printf("%s = %llu\n", path.c_str(), (unsigned long long)u); }
    void operator()(double d) const { std::printf("%s = %.9g\n", path.c_str(), d); }
    void operator()(const std::string& s) const {
      std::string e;
      for (char c : s) e += c == '\r' ? std::string("\\r") : c == '\n' ? std::string("\\n") : std::string(1, c);
      std::printf("%s = \"%s\"\n", path.c_str(), e.c_str());
    }
    void operator()(FlaskRef r) const { std::printf("%s = @%u\n", path.c_str(), r.record); }
    void operator()(const std::shared_ptr<FlaskStruct>& s) const {
      std::printf("%s : %s\n", path.c_str(), s->type.c_str());
      for (const auto& [n, f] : s->fields) print_value(path + "." + n, f);
    }
    void operator()(const std::shared_ptr<FlaskList>& l) const {
      std::printf("%s [%zu]\n", path.c_str(), l->size());
      for (size_t i = 0; i < l->size(); ++i) print_value(path + "[" + std::to_string(i) + "]", (*l)[i]);
    }
  };
  std::visit(Visitor{path}, v.v);
}

int dump_db(const std::string& data_dir) {
  Flask db(read_file(fs::path(data_dir) / "files" / "Data.bin"));
  for (uint32_t r = 0; r < db.records().size(); ++r) print_value(std::to_string(r), db.decode(r));
  return 0;
}

// Decodes every record of Data.bin the way the game's merge does.
int check_database(const fs::path& files) {
  try {
    Flask db(read_file(files / "Data.bin"));
    std::map<std::string, int> by_type;
    for (uint32_t r = 0; r < db.records().size(); ++r) {
      if (db.decode(r).is_null()) throw FormatError("record " + std::to_string(r) + " has no object");
      ++by_type[db.types()[db.records()[r].type].name];
    }
    std::printf("Data.bin: %zu types, %zu records:", db.types().size(), db.records().size());
    for (const auto& [t, n] : by_type) std::printf(" %s %d", t.c_str(), n);
    std::printf("\n");

    // The original's sin/cos with the constants from the user's image,
    // checked against the C library.
    const GameMath gm = GameMath::from_image(read_file(files.parent_path() / "xex" / "image.bin"));
    double worst = 0.0;
    for (int i = -20000; i <= 20000; ++i) {
      const double x = i * 0.00137;
      worst = std::fmax(worst, std::fabs(gm.sin(x) - std::sin(x)));
      worst = std::fmax(worst, std::fabs(gm.cos(x) - std::cos(x)));
    }
    std::printf("game sin/cos: max difference from the C library %.3g\n", worst);
    double worst_atan = 0.0;
    for (int i = -300; i <= 300; ++i)
      for (int j = -300; j <= 300; ++j) {
        const double y = i * 0.0173, x = j * 0.0191;
        worst_atan = std::fmax(worst_atan, std::fabs(gm.atan2(y, x) - std::atan2(y, x)));
      }
    std::printf("game atan2: max difference from the C library %.3g\n", worst_atan);
    double worst_tan = 0.0;
    for (int i = -1400; i <= 1400; ++i) {
      const double x = i * 0.001;  // within (-1.4, 1.4): away from the poles at +-pi/2
      worst_tan = std::fmax(worst_tan, std::fabs(gm.tan(x) - std::tan(x)));
    }
    std::printf("game tan: max difference from the C library %.3g; sin(3e8) is %s\n", worst_tan,
                std::isnan(gm.sin(3e8)) ? "NaN (as shipped)" : "not NaN");
    double worst_acos = 0.0;
    for (int i = -1000; i <= 1000; ++i) {
      const double x = i * 0.001;
      worst_acos = std::fmax(worst_acos, std::fabs(gm.acos(x) - std::acos(x)));
    }
    std::printf("game acos: max difference from the C library %.3g\n", worst_acos);

    // Build every model's line lists the way the game does at load.
    size_t models = 0, lines = 0, empty = 0;
    for (uint32_t r = 0; r < db.records().size(); ++r) {
      if (db.types()[db.records()[r].type].name != "Model") continue;
      const FlaskValue v = db.decode(r);
      const ModelGeometry g = build_model_geometry(*std::get<std::shared_ptr<FlaskStruct>>(v.v), gm);
      size_t n = 0;
      for (const auto& part : g.parts) n += part.size();
      ++models;
      lines += n;
      empty += n == 0;
    }
    std::printf("models: %zu built, %zu lines, %zu with no lines\n", models, lines, empty);
    return empty ? 1 : 0;
  } catch (const std::exception& e) {
    std::printf("Data.bin: %s\n", e.what());
    return 1;
  }
}

int verify(const std::string& data_dir) {
  const fs::path files = fs::path(data_dir) / "files";
  int bad = check_package(collect_package(files, kPrint), kPrint);
  if (!bad) bad += check_text(files);
  if (!bad) bad += check_database(files);
  std::printf(bad ? "%d problem(s)\n" : "all files good\n", bad);
  return bad ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const std::string cmd = argc > 1 ? argv[1] : "";
    if (cmd == "unpack" && argc == 4) return unpack_command(argv[2], argv[3]);
    if (cmd == "verify" && argc == 3) return verify(argv[2]);
    if (cmd == "audio" && argc == 3) {
      extract_audio(fs::path(argv[2]) / "files" / "audio", fs::path(argv[2]) / "audio", kPrint);
      return 0;
    }
    if (cmd == "dump-db" && argc == 3) return dump_db(argv[2]);
    if (cmd == "--version") {
      std::printf("geow2-extract %s\n", GEOW2_VERSION);
      return 0;
    }
    const bool help = cmd == "--help" || cmd == "-h";
    std::fprintf(help ? stdout : stderr,
                 "geow2-extract %s\n"
                 "usage:\n  geow2-extract unpack <package-file | extracted-folder | data-dir> <data-dir>\n"
                 "  geow2-extract verify <data-dir>\n  geow2-extract audio <data-dir>\n"
                 "  geow2-extract --version\n",
                 GEOW2_VERSION);
    return help ? 0 : 2;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    return 1;
  }
}

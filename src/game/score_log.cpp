#include "game/score_log.h"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <random>
#include <sstream>
#include <tuple>

#include "extract/sha1.h"

namespace geow2 {
namespace {

std::vector<std::string> fields(const std::string& line) {
  std::vector<std::string> f;
  std::istringstream s(line);
  for (std::string part; std::getline(s, part, '\t');) f.push_back(part);
  return f;
}

bool hex_id(const std::string& s) {
  return s.size() == 32 && std::all_of(s.begin(), s.end(), [](char c) {
           return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
         });
}

}  // namespace

std::string clean_name(std::string name) {
  for (char& c : name)
    if (uint8_t(c) < 0x20 || c == 0x7F) c = ' ';
  return name;
}

namespace {

// A whole number field of the log, or `fallback` when it isn't one.
int64_t number(const std::string& s, int64_t fallback = 0) {
  int64_t n = 0;
  const auto r = std::from_chars(s.data(), s.data() + s.size(), n);
  return r.ec == std::errc() && r.ptr == s.data() + s.size() ? n : fallback;
}

}  // namespace

ScoreLog ScoreLog::parse(const std::string& text) {
  ScoreLog log;
  std::istringstream in(text);
  for (std::string line; std::getline(in, line);) {
    if (!line.empty() && line.back() == '\r') line.pop_back();  // a file brought from Windows
    const std::vector<std::string> f = fields(line);
    if (f.size() < 6 || !hex_id(f[0])) continue;
    ScoreEvent e;
    e.id = f[0];
    e.mode = int(number(f[1], -1));
    e.score = number(f[2]);
    e.multiplier = int(std::clamp<int64_t>(number(f[3], 1), 0, 1000000));
    e.time = number(f[4]);
    e.device = f[5];
    e.name = f.size() > 6 ? f[6] : "";
    if (e.mode < 0 || e.mode > 5 || e.score <= 0) continue;
    log.add(std::move(e));
  }
  return log;
}

std::string ScoreLog::serialize() const {
  std::ostringstream out;
  for (const ScoreEvent& e : events_)
    out << e.id << '\t' << e.mode << '\t' << e.score << '\t' << e.multiplier << '\t' << e.time << '\t' << e.device << '\t'
        << clean_name(e.name) << '\n';
  return out.str();
}

std::vector<ScoreEvent> ScoreLog::import_board_file(const std::string& text, const std::string& device) {
  std::vector<ScoreEvent> out;
  // The same (mode, score, name) twice in one file is two games: the
  // occurrence count goes into the id.
  std::map<std::tuple<int, int64_t, std::string>, int> seen;
  std::istringstream in(text);
  for (std::string line; std::getline(in, line);) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const std::vector<std::string> f = fields(line);
    if (f.size() < 2) continue;
    ScoreEvent e;
    e.mode = int(number(f[0], -1));
    e.score = number(f[1]);
    e.name = f.size() > 2 ? f[2] : "";
    if (e.mode < 0 || e.mode > 5 || e.score <= 0) continue;
    const int n = seen[{e.mode, e.score, e.name}]++;
    const std::string key = "scores.txt\t" + std::to_string(e.mode) + '\t' + std::to_string(e.score) + '\t' + e.name +
                            '\t' + std::to_string(n);
    const auto digest = sha1({reinterpret_cast<const uint8_t*>(key.data()), key.size()});
    char hex[33];
    for (int i = 0; i < 16; ++i) std::snprintf(hex + 2 * i, 3, "%02x", digest[size_t(i)]);
    e.id = hex;
    e.device = device;
    out.push_back(std::move(e));
  }
  return out;
}

bool ScoreLog::add(ScoreEvent e) {
  if (!ids_.insert(e.id).second) return false;
  // (Both are fields of scores.log, and the name is drawn.)
  e.name = clean_name(std::move(e.name));
  e.device = clean_name(std::move(e.device));
  events_.push_back(std::move(e));
  return true;
}

size_t ScoreLog::merge(const std::vector<ScoreEvent>& events) {
  size_t added = 0;
  for (const ScoreEvent& e : events) added += add(e);
  return added;
}

std::vector<const ScoreEvent*> ScoreLog::best(int mode, size_t rows) const {
  std::vector<const ScoreEvent*> out;
  for (const ScoreEvent& e : events_)
    if (e.mode == mode) out.push_back(&e);
  std::stable_sort(out.begin(), out.end(), [](const ScoreEvent* a, const ScoreEvent* b) { return a->score > b->score; });
  if (out.size() > rows) out.resize(rows);
  return out;
}

std::string new_event_id() {
  static std::mt19937_64 rng{[] {
    std::random_device rd;
    return (uint64_t(rd()) << 32) ^ rd();
  }()};
  char hex[33];
  std::snprintf(hex, sizeof(hex), "%016llx%016llx", (unsigned long long)rng(), (unsigned long long)rng());
  return hex;
}

bool merge_progress(Profile& mine, const Profile& theirs) {
  const Profile before = mine;
  mine.unlocked |= theirs.unlocked;
  mine.millions |= theirs.millions;
  for (size_t i = 0; i < mine.progress.size(); ++i) mine.progress[i] = std::max(mine.progress[i], theirs.progress[i]);
  return mine.unlocked != before.unlocked || mine.millions != before.millions || mine.progress != before.progress;
}

}  // namespace geow2

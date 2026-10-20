#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include "game/profile.h"

namespace geow2 {

// One finished single-player game, as the local score boards and the
// online scores keep it. The boards are the best games of all known
// events, so copies of a player's progress from several devices (or a
// score server's) combine by adding the events the other side lacks: an
// event is never counted twice.
struct ScoreEvent {
  std::string id;      // 32 hex digits
  int mode = 0;        // the rules' mode, 0-5
  int64_t score = 0;
  int multiplier = 1;
  int64_t time = 0;    // seconds since 1970, UTC; 0 for rows imported from scores.txt
  std::string device;  // the device it was played on
  std::string name;    // the player's name when it was played
};

class ScoreLog {
 public:
  // scores.log: one event per line, tab-separated in the order of the
  // fields above. Lines that do not parse are skipped.
  static ScoreLog parse(const std::string& text);
  std::string serialize() const;

  // The rows of the port's earlier scores.txt (mode, score, name per
  // line). Each row gets an id made from its contents, so importing the
  // same file again (or on another device) adds nothing new.
  static std::vector<ScoreEvent> import_board_file(const std::string& text, const std::string& device);

  // Adds an event unless one with its id is known; returns whether it was new.
  bool add(ScoreEvent e);
  // Adds the events not known yet; returns how many were new.
  size_t merge(const std::vector<ScoreEvent>& events);

  const std::vector<ScoreEvent>& events() const { return events_; }
  // The best `rows` games of a mode, best first; equal scores in the
  // order they were added.
  std::vector<const ScoreEvent*> best(int mode, size_t rows) const;

 private:
  std::vector<ScoreEvent> events_;
  std::unordered_set<std::string> ids_;
};

// A new event id: 128 random bits.
std::string new_event_id();

// Control characters in a name become spaces: tabs and line breaks
// separate the log's fields, and bytes 0x01-0x1E are icons and text-size
// switches in the game's font.
std::string clean_name(std::string name);

// Another copy's progress into this one: modes unlocked there are
// unlocked here, unlock progress is the larger, the million bits add up.
// Settings stay as they are. Returns whether anything changed.
bool merge_progress(Profile& mine, const Profile& theirs);

}  // namespace geow2

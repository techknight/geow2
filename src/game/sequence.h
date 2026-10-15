#pragma once

#include <array>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "game/world.h"

namespace geow2 {

// One line of a Sequence level script (sub_82038E10 makes them, each
// command's slot 0 parses its arguments).
struct SequenceCommand {
  enum Kind { kScatter, kWait, kLine, kCircle, kClump };
  Kind kind = kWait;
  int count = 0;
  int type = 0;
  float radius = 0.0f;
  float max_radius = 0.0f;  // scatter
  float time = 0.0f;        // wait
  int wall = -1;            // line: N 0, S 1, E 2, W 3
  int half = 0;             // line
  float slip = 0.0f;        // circle, radians
  int position = 0;         // circle: 0 player, 1 centre; clump: n .. player (0..10), -1 none
};

// sub_82038C68, sub_82038E10 and the commands' parsers: one script line
// to a command; false when the line is empty, a comment or rejected.
bool parse_sequence_line(const std::string& line, SequenceCommand& out);

// sub_8203A2B8's split: a level script to its commands (lines end at CR,
// LF or ';').
std::vector<SequenceCommand> parse_sequence_script(const std::string& script);

// sub_820387C0: a unit name (any case) to a spawn type; 0 when unknown.
int sequence_type(const std::string& name);

// A command's slot 3: the geom weight of what it spawns (sub_82038450 by
// type and count; a line counts one fewer than it spawns).
int sequence_geom_weight(const SequenceCommand& c);

// Sequence (mode 2): twenty scripted levels of 30 seconds, each a script
// of spawns and waits from Data.bin's ProgressionLevel records, with a
// pause between levels (sub_8203A4F0; state at 0x8219D3E0).
class Sequence {
 public:
  // The ProgressionLevel records (level index, script), in record order.
  std::vector<std::pair<int, std::string>> scripts;

  // What the frame step shares with the spawner.
  struct Inputs {
    bool alive = false;        // sub_8204F3B0: a player is in the game
    bool over = false;         // dat_8219CE73: the game-over rule was met
    int* after_death = nullptr;     // dat_8219D288
    float* respawn_wait = nullptr;  // dat_8219CEB8
    float level_time = 30.0f;  // dat_8219D25C
    float transition = 0.0f;   // sub_82078520: the music's TransitionSwitch
  };

  // sub_82039628 (every game start): records cleared, the start level,
  // the between-levels block reset.
  void reset(World& w);
  // sub_8203A4F0, every step.
  void step(World& w, const Inputs& in);

  int shown_level = 0;     // dat_8219D3E0
  int phase = 0;           // dat_8219D3E4
  int level = 0;           // dat_8219D3E8
  size_t cursor = 0;       // dat_8219D3EC
  float wait = 0.0f;       // dat_8219D3F0
  std::vector<SequenceCommand> commands;  // dat_8219D53C
  int outcome[20] = {};    // dat_8219D548 + 0x14 * level: 1 complete, 2 died, 3 out of time
  std::function<void(uint32_t bit, int flags)> achieve;  // sub_82034CA0
  int test_level = -1;       // Level.TestLevel (record 0x8219D404, value 0x8219D424): -1, or the script every level loads
  float level_timer = 0.0f;  // dat_8219CE6C
  bool force_over = false;   // dat_8219CE71
  bool allow_end = false;    // dat_8219CE72 (the setup clears it)
  // The between-levels block (0x8219D6D8): its clock, fade, the zoom onto
  // the coming level and the end; then a panel per level, each with its
  // sideways spring {delay, pos, target, vel} (the two vertical springs
  // never move).
  float screen_clock = 0.0f, screen_fade = 0.0f, intro = 0.0f, ending = 0.0f;
  struct Spring {
    float delay = 0.0f, pos = 0.0f, target = 0.0f, vel = 0.0f;
  };
  std::array<Spring, 20> panel{};
  // The level records' statistics (0x8219D548 + 0x14 i): the geom weight
  // of the script (+4, sum of the commands' slot 3) and the geom value
  // picked up during the level (+0xC, sub_820390F0).
  std::array<int, 20> geom_target{}, geom_got{};
  // sub_820390F0: a geom picked up.
  void geom_collected(int value) {
    if (level >= 0 && level < 20) geom_got[size_t(level)] += value;
  }

 private:
  void start_level(World& w);              // sub_8203A420
  void end_level(World& w, int result);    // sub_82039708
  void next_level(World& w);               // sub_820397E8
  void clear(World& w, bool blasts);       // sub_82039300
  void reset_screen(World& w);             // sub_8203B2C8
  void run(World& w);                      // sub_82039420
  void execute(World& w, const SequenceCommand& c);
};

}  // namespace geow2

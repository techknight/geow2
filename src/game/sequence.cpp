#include "game/sequence.h"

#include <cctype>
#include <cmath>
#include <cstdlib>

#include "game/spawn.h"

namespace geow2 {
namespace {

constexpr float kTwoPow32 = 2.32830644e-10f;  // dat_820006E4

// The C runtime's ctype tests in the "C" locale: space (ctype & 8,
// sub_8212FB60) and letters or digits (ctype & 0x107, sub_8212FB80).
bool is_space(char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }
bool is_word(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; }

// _stricmp (sub_82133D60) == 0.
bool same(const std::string& a, const char* b) {
  size_t i = 0;
  for (; i < a.size() && b[i]; ++i)
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
  return i == a.size() && !b[i];
}

using Args = std::vector<std::pair<std::string, std::string>>;

// sub_82038660: the first argument of that name (any case), or "".
std::string arg(const Args& args, const char* name) {
  for (const auto& [k, v] : args)
    if (same(k, name)) return v;
  return {};
}

// sub_820386D8: whether the argument is given.
bool has(const Args& args, const char* name) {
  for (const auto& [k, v] : args)
    if (same(k, name)) return true;
  return false;
}

int to_int(const std::string& s) { return int(std::strtol(s.c_str(), nullptr, 10)); }   // sub_8212FA08
float to_float(const std::string& s) { return float(std::strtod(s.c_str(), nullptr)); }  // sub_8212FB18

// sub_82038C68: optional space, the command name (letters and digits),
// then name(value) pairs; a key without "(" and ")" rejects the line. At
// most 16 arguments (sub_820385B0).
bool split_line(const std::string& s, std::string& name, Args& args) {
  const size_t end = s.size();
  size_t p = 0;
  while (p < end && is_space(s[p])) ++p;
  size_t q = p;
  while (q < end && is_word(s[q])) ++q;
  if (q == p) return true;  // nothing on the line
  name = s.substr(p, q - p);
  for (size_t u = q; u < end; ++u) {
    if (is_space(s[u])) continue;
    size_t k = u;
    while (k < end && is_word(s[k])) ++k;
    if (k == u || k >= end) return false;
    size_t o = k;
    while (o < end && is_space(s[o])) ++o;
    if (o >= end || s[o] != '(') return false;
    const size_t v = o + 1;
    if (v >= end) return false;
    size_t c = v;
    while (c < end && s[c] != ')') ++c;
    if (c >= end) return false;
    if (args.size() >= 16) return false;
    args.emplace_back(s.substr(u, k - u), s.substr(v, c - v));
    u = c;
  }
  return true;
}

}  // namespace

int sequence_type(const std::string& name) {
  static const struct {
    const char* name;
    int type;
  } kNames[] = {{"Wanderer", 1}, {"Grunt", 2},     {"Weaver", 3},   {"Spinner", 4},  {"GravityWell", 5},
                {"Repulsar", 6}, {"Mayfly", 7},    {"Snake", 8},    {"Bacteria", 9}, {"Gyro", 10},
                {"Amoeba", 11},  {"Ricochet", 12}, {"Rocket", 12},  {"Proton", 14},  {"Swerve", 15},
                {"Quanta", 16},  {"Gate", 16},     {"Sentry", 17},  {"Sentinel", 18}, {"Crane", 19},
                {"Pin", 20},     {"Sweeper", 21},  {"Duck", 22},    {"Sneak", 25}};
  if (name.empty() || same(name, "None")) return 0;
  for (const auto& n : kNames)
    if (same(name, n.name)) return n.type;
  return 0;
}

bool parse_sequence_line(const std::string& line, SequenceCommand& c) {
  std::string name;
  Args a;
  if (!split_line(line, name, a) || name.empty()) return false;
  c = SequenceCommand{};
  if (same(name, "scatter")) {
    // Defaults 165 (dat_82000EA8) and 600 (dat_8200078C).
    c.kind = SequenceCommand::kScatter;
    c.radius = 165.0f;
    c.max_radius = 600.0f;
    c.count = to_int(arg(a, "count"));
    c.type = sequence_type(arg(a, "type"));
    if (has(a, "radius")) c.radius = to_float(arg(a, "radius"));
    if (has(a, "maxRadius")) c.max_radius = to_float(arg(a, "maxRadius"));
  } else if (same(name, "wait")) {
    c.kind = SequenceCommand::kWait;
    c.time = to_float(arg(a, "time"));
  } else if (same(name, "line")) {
    c.kind = SequenceCommand::kLine;
    const std::string wall = arg(a, "wall");
    switch (std::toupper(static_cast<unsigned char>(wall.empty() ? '\0' : wall[0]))) {
      case 'N': c.wall = 0; break;
      case 'S': c.wall = 1; break;
      case 'E': c.wall = 2; break;
      case 'W': c.wall = 3; break;
      default: break;
    }
    c.type = sequence_type(arg(a, "type"));
    c.half = to_int(arg(a, "half"));
  } else if (same(name, "circle")) {
    c.kind = SequenceCommand::kCircle;
    c.count = to_int(arg(a, "count"));
    c.radius = to_float(arg(a, "radius"));
    c.type = sequence_type(arg(a, "type"));
    c.slip = to_float(arg(a, "slip")) * 0.0174532924f;  // dat_82000F7C
    const std::string pos = arg(a, "position");
    if (same(pos, "player"))
      c.position = 0;
    else if (same(pos, "centre"))
      c.position = 1;
  } else if (same(name, "clump")) {
    c.kind = SequenceCommand::kClump;
    c.position = -1;
    static const char* kPlaces[] = {"n", "ne", "e", "se", "s", "sw", "w", "nw", "centre", "nearest", "player"};
    const std::string pos = arg(a, "position");
    for (int i = 0; i < 11; ++i)
      if (same(pos, kPlaces[i])) {
        c.position = i;
        break;
      }
    c.type = sequence_type(arg(a, "type"));
    c.radius = to_float(arg(a, "radius"));
    c.count = to_int(arg(a, "count"));
  } else {
    return false;  // unknown command: the line is dropped
  }
  return true;
}

std::vector<SequenceCommand> parse_sequence_script(const std::string& s) {
  std::vector<SequenceCommand> out;
  size_t p = 0;
  while (p < s.size()) {
    size_t e = p;
    while (e < s.size() && s[e] != '\r' && s[e] != '\n' && s[e] != ';') ++e;
    size_t n = e;
    while (n < s.size() && s[n] != '\r' && s[n] != '\n') ++n;
    while (n < s.size() && (s[n] == '\r' || s[n] == '\n')) ++n;
    SequenceCommand c;
    if (parse_sequence_line(s.substr(p, e - p), c)) out.push_back(c);
    p = n;
  }
  return out;
}

void Sequence::reset(World& w) {
  for (int& o : outcome) o = 0;
  wait = 0.0f;
  level = 0;  // Level.StartLevel (0x8219D438, default 0)
  cursor = 0;
  shown_level = level;
  reset_screen(w);
  phase = 0;
}

void Sequence::reset_screen(World& w) {
  // sub_8203B2C8: the clocks to 0 and the 20 panels' positions, with 20
  // draws (the panels are not drawn yet).
  screen_clock = screen_fade = intro = ending = 0.0f;
  for (int i = 0; i < 20; ++i) w.rng.next();
}

void Sequence::start_level(World& w) {
  (void)w;
  wait = 0.0f;
  cursor = 0;
  // sub_8203A2B8: Level.TestLevel (0x8219D404, default -1) would replace
  // the index; the first record with that level.
  commands.clear();
  const int want = test_level != -1 ? test_level : level;
  for (const auto& [index, script] : scripts)
    if (index == want) {
      commands = parse_sequence_script(script);
      break;
    }
}

void Sequence::clear(World& w, bool blasts) {
  // Units: killed as by themselves (kind 2: no score, no geoms), or just
  // removed with no player in the game. (No enemy shots exist in single
  // player.) With `blasts`, the blasts go too.
  const bool player = first_player(w) != nullptr;
  for (size_t i = 0; i < w.entities.size(); ++i) {
    Entity& e = *w.entities[i];
    if (!e.unit_based || (e.flags & 3)) continue;
    if (!player) {
      w.remove(e);
      continue;
    }
    Killer k;
    k.x = e.x;
    k.y = e.y;
    k.vx = e.vx;
    k.vy = e.vy;
    k.kind = 2;
    k.source = &e;
    e.kill(w, k);
  }
  if (blasts)
    for (auto& e : w.entities)
      if (e->type == 0xF && !(e->flags & kEntityRemoved)) w.remove(*e);
}

void Sequence::end_level(World& w, int result) {
  // Sounds 0x43 and 0x44 / 0x45 / 0x46.
  if (level < 20) outcome[level] = result;
  clear(w, result == 2);
  phase = 4;
}

void Sequence::next_level(World& w) {
  ++level;
  if (level < 20) {
    phase = 0;
  } else {
    phase = 5;
    shown_level = level;
    // Game Over (bit 4); Smile (bit 8) when exactly the levels marked here
    // were not completed (the table at sub_820397E8).
    static const int kSmile[20] = {0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 1, 1, 0};
    if (achieve) {
      achieve(0x004, 3);
      bool smile = true;
      for (int i = 0; i < 20; ++i)
        if ((outcome[i] == 1) != (kSmile[i] == 0)) {
          smile = false;
          break;
        }
      if (smile) achieve(0x008, 3);
    }
  }
  reset_screen(w);
}

void Sequence::run(World& w) {
  float d = wait;
  if (0.0f < d) {
    wait = d - w.step;
    d = wait;
  }
  if (0.5f < d) {  // dat_82000724
    if (w.enemy_count < 1) {
      wait = 0.5f;
      d = 0.5f;
    } else {
      d = wait;
    }
  }
  while (cursor < commands.size()) {
    if (0.0f < d) return;
    execute(w, commands[cursor++]);
    d = wait;
  }
}

void Sequence::execute(World& w, const SequenceCommand& c) {
  switch (c.kind) {
    case SequenceCommand::kScatter:
      for (int i = 0; i < c.count; ++i) {
        // sub_8203DD20: a right angle, then a point between radius and
        // maxRadius from every player (sub_8203BD70).
        const float angle = random_right_angle(w);
        const Vec2 p = spawn_position_between(w, c.radius, c.max_radius);
        spawn_enemy(w, c.type, angle, p.x, p.y);
      }
      break;
    case SequenceCommand::kWait:
      wait = c.time;
      break;
    case SequenceCommand::kLine:
      if (c.half == 1)
        spawn_half_wall(w, c.type, c.wall ^ 1, false);
      else if (c.half == 2)
        spawn_half_wall(w, c.type, c.wall, true);
      else
        spawn_wall(w, c.type, c.wall, false);
      break;
    case SequenceCommand::kCircle: {
      Vec2 centre{0.0f, 0.0f};
      if (c.position == 0)
        if (const PlayerTarget* p = first_player(w)) centre = {p->x, p->y};
      spawn_circle(w, c.radius, c.slip, c.type, c.count, centre);
      break;
    }
    case SequenceCommand::kClump: {
      Vec2 centre{0.0f, 0.0f};
      const float hx = w.half_x, hy = w.half_y;
      switch (c.position) {
        case 0: centre = {0.0f, -hy}; break;
        case 1: centre = {hx, -hy}; break;
        case 2: centre = {hx, 0.0f}; break;
        case 3: centre = {hx, hy}; break;
        case 4: centre = {0.0f, hy}; break;
        case 5: centre = {-hx, hy}; break;
        case 6: centre = {-hx, 0.0f}; break;
        case 7: centre = {-hx, -hy}; break;
        case 8: centre = {0.0f, 0.0f}; break;
        case 9: centre = nearest_corner(w); break;
        case 10:
          if (const PlayerTarget* p = first_player(w)) centre = {p->x, p->y};
          break;
        default: break;
      }
      spawn_clump(w, c.radius, c.type, centre, c.count);
      break;
    }
  }
}

void Sequence::step(World& w, const Inputs& in) {
  const float dt = w.step;
  // The game-over rule met before the end: the level ends (complete if a
  // player is still in, else died) and so does the game, three seconds on.
  if (in.over && !allow_end && phase != 5 && phase != 6) {
    if (level < 20) outcome[level] = in.alive ? 1 : 2;
    clear(w, true);
    phase = 6;
    reset_screen(w);
  }
  switch (phase) {
    case 0:
      if (level == 0) {
        if (1.4f < screen_clock) {  // dat_82000FF8
          shown_level = 0;
          phase = 2;  // (sound event 0x4C)
        }
      } else if (0.5f < screen_clock) {
        shown_level = level;
        phase = 1;
      }
      break;
    case 1:
      // (Or sub_82078520() > 0.5: not traced.)
      if (4.0f < screen_clock) phase = 2;  // dat_820006F4 (sound event 0x4C)
      break;
    case 2:
      intro = std::fmaf(dt, 0.6666667f, intro);  // dat_82000FF4
      if (1.0f < intro) {
        level_timer = 0.0f;
        start_level(w);
        phase = 3;
      }
      break;
    case 3:
      if (!in.alive) break;
      if (*in.after_death == 1) {
        // The last player died: the level is failed once they are back.
        *in.after_death = 0;
        if (level < 20) outcome[level] = 2;
        clear(w, true);
        phase = 4;
        *in.respawn_wait = 0.0f;
      } else if (0.0f < wait || cursor < commands.size() || 0 < w.enemy_count) {
        // The level clock (sound event 0x42 on each whole second).
        level_timer = level_timer + dt;
        if (in.level_time <= level_timer) {
          if (level < 20) outcome[level] = 3;
          clear(w, false);
          phase = 4;
          *in.respawn_wait = 0.0f;
        }
      } else {
        end_level(w, 1);
        *in.respawn_wait = 0.0f;
      }
      break;
    case 4: {
      // The next level once every geom is gone.
      bool geoms = false;
      for (const auto& e : w.entities)
        if (e->type == 0x22) {
          geoms = true;
          break;
        }
      if (!geoms) next_level(w);
      break;
    }
    case 5:
      if (4.0f <= screen_clock) phase = 6;
      break;
    case 6:
      ending = ending + dt;
      if (3.0f < ending) {  // dat_820006F8
        force_over = true;
        allow_end = true;
      }
      break;
    default:
      break;
  }
  switch (phase) {
    case 0:
    case 1:
    case 2:
    case 5:
    case 6: {
      screen_clock = dt + screen_clock;
      const float f = screen_fade + dt;
      screen_fade = 1.0f - f < 0.0f ? 1.0f : f;
      // (The panels' springs, sub_82039550: with the screen.)
      break;
    }
    case 3:
      if (in.alive) run(w);
      break;
    default:
      break;
  }
}

}  // namespace geow2

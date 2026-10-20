// geow2: the game.
//
//   geow2 [--data <data-dir>] [--profile <dir>] [--fullscreen | --windowed] [--720p | --native]
//         [--vsync | --no-vsync] [--language en|de|fr|es|it]
//   geow2 [--profile <dir>] --merge-profile <another device's profile folder>
//   geow2 [--profile <dir>] --online-pair <score server> | --online-sync
//   geow2 --data <data-dir> --frames <n> --screenshot <out.png> | --audio-out <out.wav> [--size <w> <h>]
//         [--mode <n>] [--level <n>]
//         [--stick <x> <y>] [--fire <x> <y>] [--bomb <step>] [--trigger <0-255>] [--spawn <type> <x> <y>]...
//         [--player <pad> <team> <role>]... [--versus] [--pad-stick <pad> <x> <y>] [--pad-fire <pad> <x> <y>]
//         [--inputs <csv>] [--rng-state <file> <step>] [--crt-state <hex>] [--emulator-rounding]
//         [--trace <csv>] [--positions <file>] [--dump <step> <file>] [--perf]
//         [--record <raw-file> <first-step>]
//   geow2 --data <data-dir> --frames <n> --screenshot <out.png> --frontend [--press <frame> <button>]...
//         [--online-preview unpaired|editing|waiting|failed|paired|boards|modes|results]
//   geow2 [--data <data-dir>] --setup-preview <out.png> | --setup-from <package>
//
// The first form opens the window at the front end; the profile, the
// local score boards and the display settings (display.txt: fullscreen,
// exact 720p render, vsync; F11 toggles fullscreen) are kept in --profile
// (default: the user's preferences folder). --merge-profile adds another
// device's profile folder (its games, unlocks and achievements) to it and
// quits. --online-pair pairs the profile with a group's score server
// (app/online.h; the code to confirm on a phone is printed) and syncs;
// --online-sync syncs a paired profile. Both quit after.
//
// The second runs n simulation steps of a game of --mode (0 Deadline,
// 1 Evolved, 2 Sequence, 3 Waves, 4 Pacifism, 5 King) without a window,
// with a fixed raw stick value (XInput convention), --bomb pulling the
// trigger on one step and --spawn adding enemies (type 16: a power-up
// item) at the start, and saves the final frame; --audio-out also (or
// instead) saves its sound, 5.1 float, with the options at full.
// --player joins a pad to a multiplayer game (team 0-3, role 0 alone,
// 1 pilot, 2 copilot;
// --versus for teams against each other), every pad holding the --stick
// and --fire values unless --pad-stick / --pad-fire set its own.
//
// For comparing with games recorded in an emulator of the original:
// --inputs replays the pads step by step from a CSV with a step column
// and raw lx, ly, rx, ry, lt, rt columns for pad 0 and p1_lx .. p3_rt for
// the others (a step without a row keeps the previous one; a pad without
// columns keeps its --stick / --fire). With a seq_phase column (Sequence's
// phase in the recorded game) the music's TransitionSwitch, which the
// original reads from its audio thread, is raised on the steps where that
// phase went from 1 to 2 or from 5 to 6. --rng-state loads the
// generator's state (index, then 624 words, in hex) after that step, and
// with it a <state>-spawner.txt next to it (the spawner block drawn at
// game start). --crt-state sets the C runtime rand()'s state, which the
// original keeps for the whole session; the run prints its final value.
// --emulator-rounding rounds the fused multiply-adds twice and takes the
// emulator's frsqrte (game/fused.h). --trace writes a CSV row per step
// (clock, ship, generator, Sequence state, enemy count, particles, units
// by type, score, kills, multiplier), --positions every unit, shot and
// geom after each step, --dump the units after one step. --perf also
// draws every step offscreen at --size and prints the time of each phase
// (src/app/profile.h) every 600 steps. --record appends every frame from
// that step to the last, with the HUD, to one file as raw RGBA at --size
// (for a video encoder: 60 frames a second).
//
// The third runs the front end instead, pressing a button (a, b, x, y,
// start, back, up, down, left, right; pad 0, or another with :<pad>) on
// the given frames; it never saves. --online-preview shows the Online
// scores item and screens in a fixed state, with no server.
//
// The fourth shows the setup window (src/app/setup.h), which otherwise
// appears only when no data folder is found: --setup-preview saves its
// first screen and quits, --setup-from makes the data folder (in --data,
// or the user folder) from that package without the file chooser, then
// plays.

#include <SDL3/SDL.h>
// On Windows the game is a window program: SDL provides its WinMain.
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "app/console.h"
#include "app/frontend.h"
#include "app/profile.h"
#include "game/fused.h"
#include "audio/device.h"
#include "audio/engine.h"
#include "audio/game_audio.h"
#include "app/game.h"
#include "app/png.h"
#include "app/renderer.h"
#include "app/online.h"
#include "app/setup.h"
#include "data/png_read.h"
#include "extract/bytes.h"
#include "game/gravity_well.h"
#include "game/score_log.h"
#include "game/spawn.h"
#include "third_party/qrcodegen/qrcodegen.hpp"
#include "version.h"

namespace fs = std::filesystem;
using namespace geow2;

namespace {

struct DevSpawn {
  int type;
  float x, y;
};

struct Press {
  int frame;
  int axis;
  int pad = 0;
};

int16_t clamp16(int v) { return int16_t(std::clamp(v, -32768, 32767)); }

// The folder holding the shaders and the window's icon: next to the program
// (the release archives, geow2.app's Resources), else ../share/geow2 (an
// install with GEOW2_UNIX_LAYOUT, such as the Flatpak).
fs::path resource_dir() {
  const fs::path base = fs::path(SDL_GetBasePath() ? SDL_GetBasePath() : "");
  std::error_code ec;
  if (fs::exists(base / "shaders", ec)) return base;
  const fs::path share = base.parent_path().parent_path() / "share" / "geow2";
  if (fs::exists(share / "shaders", ec)) return share;
  return base;
}

// The files in the profile folder are replaced whole (written beside, then
// renamed over), so the game crashing or a full disk never leaves half a
// file (the data is not synced to the disk before the rename: a power cut
// at that moment can still lose it).
void save_text(const fs::path& path, const std::string& s) {
  if (!replace_file(path, std::span(reinterpret_cast<const uint8_t*>(s.data()), s.size())))
    SDL_Log("cannot save %s", path.string().c_str());
}

// The profile block and the local boards on disk.
Profile load_profile(const fs::path& dir) {
  std::array<uint8_t, 0x30> b{};
  std::ifstream f(dir / "profile.bin", std::ios::binary);
  if (!f || !f.read(reinterpret_cast<char*>(b.data()), std::streamsize(b.size()))) return Profile{};
  Profile p = Profile::from_bytes(b);
  if (p.repair()) SDL_Log("profile.bin had settings out of range; those are back to their defaults");
  return p;
}

void save_profile(const fs::path& dir, const Profile& p) {
  const auto b = p.to_bytes();
  if (!replace_file(dir / "profile.bin", b)) SDL_Log("cannot save %s", (dir / "profile.bin").string().c_str());
}

std::string read_text(const fs::path& path) {
  std::ifstream f(path, std::ios::binary);
  std::ostringstream s;
  s << f.rdbuf();
  return s.str();
}

// This device's id in the score log: random, made once.
std::string device_id(const fs::path& dir) {
  std::istringstream s(read_text(dir / "device.txt"));
  std::string id;
  if (s >> id && id.size() == 32) return id;
  id = new_event_id();
  save_text(dir / "device.txt", id + "\n");
  return id;
}

// scores.log: every finished single-player game (game/score_log.h). An
// earlier version's scores.txt is imported when there is no log yet.
ScoreLog load_score_log(const fs::path& dir, const std::string& device) {
  if (fs::exists(dir / "scores.log")) return ScoreLog::parse(read_text(dir / "scores.log"));
  ScoreLog log;
  log.merge(ScoreLog::import_board_file(read_text(dir / "scores.txt"), device));
  return log;
}

void save_score_log(const fs::path& dir, const ScoreLog& log) { save_text(dir / "scores.log", log.serialize()); }

// The boards: each mode's best games in the log.
void boards_from(const ScoreLog& log, std::array<LocalBoard, 6>& boards) {
  for (int m = 0; m < 6; ++m) {
    auto& rows = boards[size_t(m)].rows;
    rows.clear();
    for (const ScoreEvent* e : log.best(m, LocalBoard::kRows)) rows.push_back({e->score, e->name});
  }
}

// The unlocked achievements (bits of Game::achieved), kept beside the profile.
uint32_t load_achievements(const fs::path& dir) {
  std::istringstream s(read_text(dir / "achievements.txt"));
  unsigned long bits = 0;
  s >> std::hex >> bits;
  return uint32_t(bits);
}

void save_achievements(const fs::path& dir, uint32_t bits) {
  char line[16];
  std::snprintf(line, sizeof(line), "%x\n", unsigned(bits));
  save_text(dir / "achievements.txt", line);
}

// A QR code of `text` as a texture: black modules on white, eight pixels a
// module (so the texture's filtering keeps the edges sharp), with the
// standard four-module quiet zone. A text no QR code holds gives a blank
// square (the pairing screen still shows the code to type).
Image qr_image(const std::string& text) {
  std::optional<qrcodegen::QrCode> made;
  try {
    made = qrcodegen::QrCode::encodeText(text.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
  } catch (const std::exception&) {
    Image blank;
    blank.width = blank.height = 8;
    blank.rgba.assign(8 * 8 * 4, 255);
    return blank;
  }
  const qrcodegen::QrCode& q = *made;
  const int n = q.getSize() + 8, px = 8;
  Image img;
  img.width = img.height = uint32_t(n * px);
  img.rgba.assign(size_t(img.width) * img.height * 4, 255);
  for (int y = 0; y < q.getSize(); ++y)
    for (int x = 0; x < q.getSize(); ++x) {
      if (!q.getModule(x, y)) continue;
      for (int dy = 0; dy < px; ++dy)
        for (int dx = 0; dx < px; ++dx) {
          const size_t at = (size_t((y + 4) * px + dy) * img.width + size_t((x + 4) * px + dx)) * 4;
          img.rgba[at] = img.rgba[at + 1] = img.rgba[at + 2] = 0;
        }
    }
  return img;
}

// The progress a score server merges, from and back into the profile block.
OnlineProgress online_progress(const Profile& p, uint32_t achievements) {
  OnlineProgress o;
  o.unlocked = p.unlocked;
  o.millions = p.millions;
  o.progress = p.progress;
  o.achievements = achievements;
  return o;
}

void apply_online_progress(Profile& p, const OnlineProgress& o) {
  p.unlocked = o.unlocked;
  p.millions = o.millions;
  p.progress = o.progress;
}

// One row per line: mode, score, name (tab-separated). Kept beside
// scores.log for earlier versions of the game.
void save_boards(const fs::path& dir, const std::array<LocalBoard, 6>& boards) {
  std::ostringstream f;
  for (int m = 0; m < 6; ++m)
    for (const auto& r : boards[size_t(m)].rows) f << m << '\t' << r.score << '\t' << r.name << '\n';
  save_text(dir / "scores.txt", f.str());
}

// --merge-profile: another device's profile folder added to this one: its
// games join the score log, its unlocks and achievements add to these.
// Settings stay. Merging the same folder again changes nothing.
int merge_profile(const fs::path& dir, const fs::path& other) {
  if (!fs::is_directory(other)) {
    std::fprintf(stderr, "no profile folder at %s\n", other.string().c_str());
    return 1;
  }
  fs::create_directories(dir);
  ScoreLog log = load_score_log(dir, device_id(dir));
  std::istringstream other_device(read_text(other / "device.txt"));
  std::string their_device = "imported";
  other_device >> their_device;
  const size_t games = log.merge(load_score_log(other, their_device).events());
  Profile mine = load_profile(dir);
  const bool progress = merge_progress(mine, load_profile(other));
  const uint32_t had = load_achievements(dir), achieved = had | load_achievements(other);
  save_score_log(dir, log);
  std::array<LocalBoard, 6> boards;
  boards_from(log, boards);
  save_boards(dir, boards);
  if (progress) save_profile(dir, mine);
  if (achieved != had) save_achievements(dir, achieved);
  std::printf("merged %s into %s: %zu new game(s), %s, %d new achievement(s)\n", other.string().c_str(),
              dir.string().c_str(), games, progress ? "unlocks or progress added" : "no new unlocks or progress",
              std::popcount(achieved & ~had));
  return 0;
}

// --online-pair <server> pairs this device from a terminal (the code to
// confirm on a signed-in phone is printed) and syncs; --online-sync syncs
// a paired device. Neither opens a window.
int online_command(const fs::path& dir, const std::string& pair_server) {
  fs::create_directories(dir);
  const std::string device = device_id(dir);
  ScoreLog log = load_score_log(dir, device);
  Profile profile = load_profile(dir);
  uint32_t achievements = load_achievements(dir);
  OnlineProgress progress = online_progress(profile, achievements);
  Online online(dir);
  if (!pair_server.empty()) {
    const std::string base = server_base(pair_server);
    if (base.empty()) {
      std::fprintf(stderr, "not a server address: %s\n", pair_server.c_str());
      return 1;
    }
    online.start_pairing(base, Online::device_name());
    bool shown = false;
    for (;;) {
      const Online::PairingView p = online.pairing();
      if (p.state == Online::Pairing::kWaiting && !shown) {
        std::printf("Confirm this code on your phone: %s\n  %s\n", p.code.c_str(), p.url.c_str());
        std::fflush(stdout);
        shown = true;
      }
      if (p.state == Online::Pairing::kFailed) {
        std::fprintf(stderr, "pairing failed: %s\n", p.error.c_str());
        return 1;
      }
      if (online.update(log, progress, achievements)) break;  // paired: its first sync has started
      SDL_Delay(200);
    }
    std::printf("paired as %s\n", online.state().player_name.c_str());
  } else {
    if (!online.paired()) {
      std::fprintf(stderr, "this device is not paired (use --online-pair <server>)\n");
      return 1;
    }
    online.sync(log, progress);
  }
  size_t games = 0;
  uint32_t new_achievements = 0;
  bool changed = false;
  while (online.syncing() || !online.state().token.empty()) {
    if (auto a = online.update(log, progress, achievements)) {
      games += a->new_games;
      new_achievements |= a->new_achievements;
      changed |= a->progress;
    }
    if (!online.syncing()) break;
    SDL_Delay(100);
  }
  if (const std::string error = online.error(); !error.empty()) {
    std::fprintf(stderr, "sync failed: %s\n", error.c_str());
    return 1;
  }
  save_score_log(dir, log);
  std::array<LocalBoard, 6> boards;
  boards_from(log, boards);
  save_boards(dir, boards);
  apply_online_progress(profile, progress);
  if (changed) save_profile(dir, profile);
  if (new_achievements) save_achievements(dir, achievements);
  std::printf("synced with %s (%s) as %s: %zu new game(s) from the server, %s, %d new achievement(s)\n",
              online.state().server.c_str(), online.state().group.c_str(), online.state().player_name.c_str(), games,
              changed ? "unlocks or progress added" : "no new unlocks or progress", std::popcount(new_achievements));
  static const char* const kModes[6] = {"Deadline", "Evolved", "Sequence", "Waves", "Pacifism", "King"};
  const auto group = online.group_boards();
  for (int m = 0; m < 6; ++m)
    for (size_t i = 0; i < group[size_t(m)].size() && i < 3; ++i)
      std::printf("  %-9s %zu. %-20s %lld\n", kModes[m], i + 1, group[size_t(m)][i].name.c_str(),
                  (long long)group[size_t(m)][i].score);
  return 0;
}

// The game's sticks from the player's pad, as the player update reads them
// (sub_8204FF00): movement on axes (bit 0 << 1) and +1, firing on bit 1
// and +1, the bomb on the triggers or (bit 2) the bumpers; XInput
// convention (y up positive).
Sticks sticks_from(const PadState& p, const Profile& profile) {
  const size_t mv = (profile.controls & 1) << 1, fr = profile.controls & 2;
  const size_t bomb = (profile.controls & 4) ? 10 : 8;
  auto raw = [](float v) { return clamp16(int(v * 32767.0f)); };
  Sticks s;
  s.move_x = raw(p.axis[mv]);
  s.move_y = raw(-p.axis[mv + 1]);
  s.fire_x = raw(p.axis[fr]);
  s.fire_y = raw(-p.axis[fr + 1]);
  s.left_trigger = uint8_t(std::clamp(int(p.axis[bomb] * 255.0f), 0, 255));
  s.right_trigger = uint8_t(std::clamp(int(p.axis[bomb + 1] * 255.0f), 0, 255));
  return s;
}

// A connected SDL gamepad as the original's axes.
PadState pad_state(SDL_Gamepad* pad) {
  PadState p;
  if (!pad) return p;
  p.connected = true;
  auto axis = [&](SDL_GamepadAxis a) { return std::clamp(float(SDL_GetGamepadAxis(pad, a)) / 32767.0f, -1.0f, 1.0f); };
  auto button = [&](SDL_GamepadButton b) { return SDL_GetGamepadButton(pad, b) ? 1.0f : 0.0f; };
  // SDL's Y axes point down, as the original's axes 1 and 3.
  p.axis[0] = axis(SDL_GAMEPAD_AXIS_LEFTX);
  p.axis[1] = axis(SDL_GAMEPAD_AXIS_LEFTY);
  p.axis[2] = axis(SDL_GAMEPAD_AXIS_RIGHTX);
  p.axis[3] = axis(SDL_GAMEPAD_AXIS_RIGHTY);
  p.axis[4] = button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT) - button(SDL_GAMEPAD_BUTTON_DPAD_LEFT);
  p.axis[5] = button(SDL_GAMEPAD_BUTTON_DPAD_DOWN) - button(SDL_GAMEPAD_BUTTON_DPAD_UP);
  p.axis[8] = axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
  p.axis[9] = axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
  p.axis[10] = button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
  p.axis[11] = button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
  p.axis[0xC] = button(SDL_GAMEPAD_BUTTON_SOUTH);
  p.axis[0xD] = button(SDL_GAMEPAD_BUTTON_EAST);
  p.axis[0xE] = button(SDL_GAMEPAD_BUTTON_WEST);
  p.axis[0xF] = button(SDL_GAMEPAD_BUTTON_NORTH);
  p.axis[0x10] = button(SDL_GAMEPAD_BUTTON_START);
  p.axis[0x11] = button(SDL_GAMEPAD_BUTTON_BACK);
  return p;
}

// The keyboard as a pad (DIFFERENCES.md "Input devices"): WASD the left
// stick, the arrows the right stick and the D-pad, Space the right
// trigger, Enter A, Escape START while playing and B otherwise, X and Y.
void keyboard_pad(PadState& p, bool playing) {
  const bool* k = SDL_GetKeyboardState(nullptr);
  auto set = [&](size_t a, float v) {
    if (v != 0.0f) p.axis[a] = v;
  };
  set(0, float(k[SDL_SCANCODE_D]) - float(k[SDL_SCANCODE_A]));
  set(1, float(k[SDL_SCANCODE_S]) - float(k[SDL_SCANCODE_W]));
  const float rx = float(k[SDL_SCANCODE_RIGHT]) - float(k[SDL_SCANCODE_LEFT]);
  const float ry = float(k[SDL_SCANCODE_DOWN]) - float(k[SDL_SCANCODE_UP]);
  set(2, rx);
  set(3, ry);
  set(4, rx);
  set(5, ry);
  if (k[SDL_SCANCODE_SPACE]) p.axis[9] = 1.0f;
  if (k[SDL_SCANCODE_RETURN]) p.axis[0xC] = 1.0f;
  if (k[SDL_SCANCODE_ESCAPE]) p.axis[playing ? 0x10 : 0xD] = 1.0f;
  if (k[SDL_SCANCODE_X]) p.axis[0xE] = 1.0f;
  if (k[SDL_SCANCODE_Y]) p.axis[0xF] = 1.0f;
  p.connected = true;
}

// A headless step's commands. Nothing paces a run without a window, so
// every 30 steps it waits for the GPU: a slow or software one (lavapipe)
// would otherwise queue work, and memory, without bound.
void submit_headless(SDL_GPUDevice* dev, SDL_GPUCommandBuffer* cmd, int step) {
  if (step % 30 != 29) {
    SDL_SubmitGPUCommandBuffer(cmd);
    return;
  }
  if (SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd)) {
    SDL_WaitForGPUFences(dev, true, &fence, 1);
    SDL_ReleaseGPUFence(dev, fence);
  }
}

// The frame into `target` (w x h): rendered at that size, or with
// `exact_720p` at 1280 x 720 into `frame720` (made on first use) and scaled
// (bilinear) into the 16:9 area of the target.
void draw_frame(SDL_GPUDevice* dev, Renderer& r, SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* target,
                SDL_GPUTextureFormat format, uint32_t w, uint32_t h, const Game& g, const UiCanvas& ui,
                bool exact_720p, SDL_GPUTexture*& frame720) {
  if (!exact_720p) {
    render(r, cmd, target, format, w, h, g, ui);
    return;
  }
  if (!frame720) {
    SDL_GPUTextureCreateInfo tci{};
    tci.type = SDL_GPU_TEXTURETYPE_2D;
    tci.format = format;
    tci.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    tci.width = 1280;
    tci.height = 720;
    tci.layer_count_or_depth = 1;
    tci.num_levels = 1;
    frame720 = SDL_CreateGPUTexture(dev, &tci);
    if (!frame720) return;
  }
  render(r, cmd, frame720, format, 1280, 720, g, ui);
  uint32_t pw = w, ph = uint32_t(float(w) * 9.0f / 16.0f + 0.5f);
  if (ph > h) {
    ph = h;
    pw = uint32_t(float(h) * 16.0f / 9.0f + 0.5f);
  }
  SDL_GPUBlitInfo blit{};
  blit.source.texture = frame720;
  blit.source.w = 1280;
  blit.source.h = 720;
  blit.destination.texture = target;
  blit.destination.x = (w - pw) / 2;
  blit.destination.y = (h - ph) / 2;
  blit.destination.w = pw;
  blit.destination.h = ph;
  blit.load_op = SDL_GPU_LOADOP_CLEAR;
  blit.clear_color = {0, 0, 0, 1};
  blit.filter = SDL_GPU_FILTER_LINEAR;
  SDL_BlitGPUTexture(cmd, &blit);
}

// The frame as a PNG at `out`, or appended to `raw` as w x h RGBA bytes.
int capture(SDL_GPUDevice* dev, Renderer& r, const Game& g, const UiCanvas& ui, const fs::path& out, uint32_t w,
            uint32_t h, bool exact_720p = false, std::ofstream* raw = nullptr) {
  SDL_GPUTextureCreateInfo tci{};
  tci.type = SDL_GPU_TEXTURETYPE_2D;
  tci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
  tci.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
  tci.width = w;
  tci.height = h;
  tci.layer_count_or_depth = 1;
  tci.num_levels = 1;
  SDL_GPUTexture* tex = SDL_CreateGPUTexture(dev, &tci);
  SDL_GPUTransferBufferCreateInfo dl{SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, w * h * 4, 0};
  SDL_GPUTransferBuffer* down = SDL_CreateGPUTransferBuffer(dev, &dl);
  if (!tex || !down) {
    SDL_Log("cannot create the offscreen target: %s", SDL_GetError());
    return 1;
  }
  SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev);
  SDL_GPUTexture* frame720 = nullptr;
  draw_frame(dev, r, cmd, tex, tci.format, w, h, g, ui, exact_720p, frame720);
  SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
  SDL_GPUTextureRegion src{};
  src.texture = tex;
  src.w = w;
  src.h = h;
  src.d = 1;
  SDL_GPUTextureTransferInfo dst{down, 0, w, h};
  SDL_DownloadFromGPUTexture(copy, &src, &dst);
  SDL_EndGPUCopyPass(copy);
  SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
  SDL_WaitForGPUFences(dev, true, &fence, 1);
  SDL_ReleaseGPUFence(dev, fence);
  const auto* px = static_cast<const uint8_t*>(SDL_MapGPUTransferBuffer(dev, down, false));
  if (raw)
    raw->write(reinterpret_cast<const char*>(px), std::streamsize(size_t(w) * h * 4));
  else
    write_png(out, w, h, {px, size_t(w) * h * 4});
  SDL_UnmapGPUTransferBuffer(dev, down);
  SDL_ReleaseGPUTransferBuffer(dev, down);
  SDL_ReleaseGPUTexture(dev, tex);
  if (frame720) SDL_ReleaseGPUTexture(dev, frame720);
  return 0;
}

// --inputs: the pads' sticks for each step (index = step number, 1-based);
// a pad without columns is left out.
struct StepInputs {
  std::array<bool, 4> has{};
  std::vector<std::array<Sticks, 4>> steps;
  std::vector<int> phase;  // Sequence's phase after each step, -1 unknown
};

StepInputs load_inputs(const fs::path& path) {
  StepInputs out;
  std::ifstream f(path);
  std::string line;
  if (!std::getline(f, line)) {
    SDL_Log("cannot read the inputs %s", path.string().c_str());
    return out;
  }
  const auto split = [](const std::string& l) {
    std::vector<std::string> v;
    std::stringstream ss(l);
    for (std::string c; std::getline(ss, c, ',');) v.push_back(c);
    return v;
  };
  const std::vector<std::string> head = split(line);
  const auto col = [&](const std::string& name) {
    const auto it = std::find(head.begin(), head.end(), name);
    return it == head.end() ? -1 : int(it - head.begin());
  };
  const int cs = col("step");
  const int cph = col("seq_phase");
  // Per pad: lx, ly, rx, ry, lt, rt.
  std::array<std::array<int, 6>, 4> cols{};
  for (int p = 0; p < 4; ++p) {
    const std::string prefix = p == 0 ? "" : "p" + std::to_string(p) + "_";
    const char* names[6] = {"lx", "ly", "rx", "ry", "lt", "rt"};
    out.has[size_t(p)] = true;
    for (int k = 0; k < 6; ++k) {
      cols[size_t(p)][size_t(k)] = col(prefix + names[k]);
      if (cols[size_t(p)][size_t(k)] < 0) out.has[size_t(p)] = false;
    }
  }
  if (cs < 0 || !out.has[0]) {
    SDL_Log("%s: needs step, lx, ly, rx, ry, lt, rt columns", path.string().c_str());
    out.has = {};
    return out;
  }
  while (std::getline(f, line)) {
    const std::vector<std::string> v = split(line);
    if (int(v.size()) < int(head.size())) continue;
    const int step = std::atoi(v[size_t(cs)].c_str());
    if (step < 0) continue;
    std::array<Sticks, 4> row{};
    for (size_t p = 0; p < 4; ++p) {
      if (!out.has[p]) continue;
      const auto num = [&](int k) { return std::atoi(v[size_t(cols[p][size_t(k)])].c_str()); };
      Sticks& st = row[p];
      st.move_x = clamp16(num(0));
      st.move_y = clamp16(num(1));
      st.fire_x = clamp16(num(2));
      st.fire_y = clamp16(num(3));
      st.left_trigger = uint8_t(std::clamp(num(4), 0, 255));
      st.right_trigger = uint8_t(std::clamp(num(5), 0, 255));
    }
    if (size_t(step) >= out.steps.size()) {
      const std::array<Sticks, 4> hold = out.steps.empty() ? std::array<Sticks, 4>{} : out.steps.back();
      out.steps.resize(size_t(step) + 1, hold);
    }
    out.steps[size_t(step)] = row;
    if (cph >= 0) {
      if (size_t(step) >= out.phase.size()) out.phase.resize(size_t(step) + 1, -1);
      // A blank phase (a row with only the pads) keeps the one before.
      const std::string& ph = v[size_t(cph)];
      out.phase[size_t(step)] = !ph.empty() ? std::atoi(ph.c_str()) : step > 0 ? out.phase[size_t(step) - 1] : -1;
    }
  }
  return out;
}

// The recorded game's Sequence phase moved on from 1 or 5 at this step.
bool transition_at(const StepInputs& in, size_t step) {
  if (step == 0 || step >= in.phase.size()) return false;
  const int a = in.phase[step - 1], b = in.phase[step];
  return (a == 1 && b == 2) || (a == 5 && b == 6);
}

// The sound's hooks into the game. The events behind rate gates or in
// alternating pairs go through the glue with the millisecond clock.
void attach_sound(Game& g, audio::GameAudio& glue, std::function<uint32_t()> clock) {
  g.world->sound = [&glue, clock](audio::Event e) {
    const uint32_t now = clock();
    switch (e) {
      case audio::Event::kPickupGeom1: glue.pickup_geom(now); break;
      case audio::Event::kGateBounceBullet1: glue.gate_bounce_bullet(now); break;
      case audio::Event::kPinRicochet1: glue.pin_ricochet(now); break;
      case audio::Event::kPowerUpEndSpeed:
      case audio::Event::kPowerUpEndSlow:
      case audio::Event::kPowerUpEndMagnet:
      case audio::Event::kPowerUpEndWeapon:
      case audio::Event::kPowerUpEndFreeze: glue.power_up_end(e, now); break;
      default: glue.fire(e); break;
    }
  };
  g.world->repulsar_hit = [&glue, clock]() { glue.repulsar_hit_wall(clock()); };
  g.world->well_hum = [&glue](const Entity* w, int hits) { glue.gravity_well_hits(reinterpret_cast<uintptr_t>(w), hits); };
  g.music = [&g, &glue](bool in_game) { glue.select_music(in_game, g.rules.mode, frozen(g)); };
}

// The sound after a step: sub_82078970 after the wells, then sub_82079BC0.
void sound_step(Game& g, audio::GameAudio& glue, const audio::AudioEngine& sound, float sfx_option,
                float music_option) {
  if (!frozen(g)) glue.gravity_wells_update(kWellMaxHits);
  const GameAudioState a = audio_state(g);
  audio::AudioFrame f;
  f.paused = a.paused;
  f.sfx_option = sfx_option;
  f.music_option = music_option;
  f.distance_from_hill = a.distance_from_hill;
  f.thrust = a.thrust;
  f.magnet_active = a.magnet_active;
  f.player_x = a.player_x;
  f.player_y = a.player_y;
  f.progression_level = a.progression_level;
  f.multiplier = a.multiplier;
  f.score = a.score;
  f.no_enemies = a.no_enemies;
  f.time_left = a.time_left;
  f.level_time_left = a.level_time_left;
  f.bomb_kill_count = a.bomb_kills;
  glue.frame(f);
  g.transition_switch = sound.transition_switch();
}

// --audio-out: interleaved 5.1 float samples as a WAVE_FORMAT_EXTENSIBLE file.
void write_wav(const fs::path& path, const std::vector<float>& samples) {
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  auto u16 = [&](uint32_t v) { f.put(char(v & 0xFF)).put(char(v >> 8 & 0xFF)); };
  auto u32 = [&](uint32_t v) {
    u16(v & 0xFFFF);
    u16(v >> 16);
  };
  const uint32_t data = uint32_t(samples.size() * sizeof(float));
  f.write("RIFF", 4);
  u32(4 + 8 + 40 + 8 + data);
  f.write("WAVEfmt ", 8);
  u32(40);
  u16(0xFFFE);  // WAVE_FORMAT_EXTENSIBLE
  u16(6);
  u32(uint32_t(audio::kSampleRate));
  u32(uint32_t(audio::kSampleRate) * 6 * 4);
  u16(6 * 4);
  u16(32);
  u16(22);
  u16(32);
  u32(0x3F);  // FL FR FC LFE BL BR
  // KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
  const uint8_t guid[16] = {0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
                            0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
  f.write(reinterpret_cast<const char*>(guid), 16);
  f.write("data", 4);
  u32(data);
  f.write(reinterpret_cast<const char*>(samples.data()), std::streamsize(data));
}

int run_headless(SDL_GPUDevice* dev, Renderer& r, Game& g, int frames, const std::array<Sticks, 4>& sticks,
                 const StepInputs& inputs, int bomb_step, const std::vector<DevSpawn>& spawns, const fs::path& out, uint32_t w, uint32_t h,
                 bool exact_720p, const fs::path& trace_path, const fs::path& rng_path, int rng_step,
                 const fs::path& dump_path, int dump_step, const fs::path& positions_path, const fs::path& data_dir,
                 const fs::path& audio_path, const fs::path& record_path, int record_from) {
  // --trace: after each step, the clock and slot 0's object (x, y, vx,
  // vy) at full precision, as memory reads of the original in an emulator give them.
  std::ofstream trace;
  if (!trace_path.empty()) {
    trace.open(trace_path, std::ios::trunc);
    trace << "step,clock,x,y,vx,vy,rng_index,rng_word0,seq_phase,seq_wait,enemies,particles,units,score,kills,multiplier\n";
  }
  // Type 16 (0x10) is a power-up item; the rest are enemies.
  for (const DevSpawn& d : spawns)
    if (d.type == 0x10)
      g.world->add(std::make_unique<PowerUp>(*g.world, d.x, d.y, 3));
    else
      spawn_enemy(*g.world, d.type, 0.0f, d.x, d.y);
  // --positions: every unit (creation order) after each step, as
  // "step type:x:y ...".
  std::ofstream positions;
  if (!positions_path.empty()) positions.open(positions_path, std::ios::trunc);
  // --audio-out: the sound of each step (800 samples at 48 kHz), with the
  // options' volumes at full and a clock of whole steps.
  audio::AudioEngine sound;
  audio::GameAudio glue(sound);
  std::vector<float> samples;
  int step_now = 0;
  if (!audio_path.empty()) {
    std::string error;
    if (sound.init(data_dir, &error)) {
      glue.start();
      attach_sound(g, glue, [&step_now]() { return uint32_t(uint64_t(step_now) * 50 / 3); });
      glue.select_music(true, g.rules.mode, frozen(g));  // the game's start, before the hooks existed
    } else {
      SDL_Log("no sound: %s", error.c_str());
    }
  }
  // --perf: every step is also drawn (offscreen, at --size) and waited
  // for, and the phase times are reported every 600 steps.
  SDL_GPUTexture* prof_target = nullptr;
  const UiCanvas prof_ui{};
  if (g_profile.on) {
    SDL_GPUTextureCreateInfo tci{};
    tci.type = SDL_GPU_TEXTURETYPE_2D;
    tci.format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
    tci.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    tci.width = w;
    tci.height = h;
    tci.layer_count_or_depth = 1;
    tci.num_levels = 1;
    prof_target = SDL_CreateGPUTexture(dev, &tci);
  }
  // The in-game screen (the HUD), drawn for --record and the final frame.
  Profile profile;
  std::array<LocalBoard, 6> boards;
  Frontend fe(g, g.text, profile, boards, {});
  fe.watch_game();
  // --record: every frame from that step on, as raw RGBA at --size.
  std::ofstream record;
  if (!record_path.empty()) record.open(record_path, std::ios::binary | std::ios::trunc);
  for (int i = 0; i < frames; ++i) {
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev);
    // By joined index, from each joined pad.
    std::array<Sticks, 4> pads = sticks;
    if (!inputs.steps.empty()) {
      const std::array<Sticks, 4>& row = inputs.steps[std::min(size_t(i) + 1, inputs.steps.size() - 1)];
      for (size_t p = 0; p < 4; ++p)
        if (inputs.has[p]) pads[p] = row[p];
      if (!inputs.phase.empty()) g.transition_switch = transition_at(inputs, size_t(i) + 1) ? 1.0f : 0.0f;
    }
    std::array<Sticks, 4> s{};
    for (int j = 0; j < g.joined_count && j < 4; ++j) s[size_t(j)] = pads[size_t(g.joined_pad[size_t(j)] & 3)];
    if (i == bomb_step) s[0].right_trigger = 255;
    if (prof_target) {
      {
        PROFILE("frame (cpu)");
        tick(g, std::span<const Sticks>(s.data(), size_t(std::max(g.joined_count, 1))), r, cmd);
        render(r, cmd, prof_target, SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM, w, h, g, prof_ui);
      }
      SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
      {
        PROFILE("gpu wait");
        if (fence) SDL_WaitForGPUFences(dev, true, &fence, 1);
      }
      if (fence) SDL_ReleaseGPUFence(dev, fence);
      g_profile.end_frame(i + 1);
      if ((i + 1) % 600 == 0) {
        char label[32];
        std::snprintf(label, sizeof(label), "steps %d-%d", i - 598, i + 1);
        g_profile.report(label);
      }
    } else {
      tick(g, std::span<const Sticks>(s.data(), size_t(std::max(g.joined_count, 1))), r, cmd);
      submit_headless(dev, cmd, i);
    }
    if (sound.loaded()) {
      step_now = i + 1;
      sound_step(g, glue, sound, 1.0f, 1.0f);
      const size_t at = samples.size();
      samples.resize(at + 800 * 6);
      sound.render(samples.data() + at, 800);
    }
    if (record.is_open() && i + 1 >= record_from) {
      UiCanvas ui;
      fe.draw(ui);
      if (capture(dev, r, g, ui, {}, w, h, exact_720p, &record)) return 1;
    }
    // --rng-state: the generator's state read from the original in an
    // emulator (the index, then 624 words, in hex), loaded after that step.
    if (!rng_path.empty() && i + 1 == rng_step) {
      std::ifstream f(rng_path);
      uint32_t words[GameRng::kWords] = {};
      int index = 0;
      f >> std::hex >> index;
      for (auto& wd : words) f >> std::hex >> wd;
      if (f) g.rng.load(words, index);
      else SDL_Log("cannot read the generator state %s", rng_path.string().c_str());
      // With it, the spawner state the game drew before that step
      // (0x8219DDA0, 0x30 bytes in hex, big-endian), when the file exists.
      fs::path sp = rng_path;
      sp.replace_filename(rng_path.stem().string() + "-spawner.txt");
      if (std::ifstream sf(sp); sf) {
        uint8_t b[0x30] = {};
        for (auto& v : b) {
          unsigned x = 0;
          sf >> std::hex >> x;
          v = uint8_t(x);
        }
        if (sf) {
          auto u32 = [&](int o) { return uint32_t(b[o]) << 24 | uint32_t(b[o + 1]) << 16 | uint32_t(b[o + 2]) << 8 | b[o + 3]; };
          auto f32 = [&](int o) {
            const uint32_t v = u32(o);
            float r;
            std::memcpy(&r, &v, 4);
            return r;
          };
          WaveSpawner& ws = g.waves;
          for (int k = 0; k < 4; ++k) {
            ws.side[k] = int(u32(4 * k));
            ws.full[k] = b[0x10 + k] != 0;
          }
          ws.index = int(u32(0x14));
          ws.flip = b[0x18] != 0;
          ws.current_full = b[0x19] != 0;
          ws.progress = f32(0x1C);
          ws.timer = f32(0x20);
          ws.state = int(u32(0x24));
          ws.group_type = int(u32(0x28));
          ws.group_count = int(u32(0x2C));
        }
      }
    }
    // --dump: every unit (creation order) after that step: type, x, y.
    if (!dump_path.empty() && i + 1 == dump_step) {
      std::ofstream f(dump_path, std::ios::trunc);
      for (const auto& e : g.world->entities) {
        if (!e->unit_based || (e->flags & kEntityRemoved)) continue;
        char line[128];
        std::snprintf(line, sizeof(line), "%d %.9g %.9g\n", e->type, double(e->x), double(e->y));
        f << line;
      }
    }
    if (positions.is_open()) {
      positions << (i + 1);
      for (const auto& e : g.world->entities) {
        if (!e->unit_based || (e->flags & kEntityRemoved)) continue;
        char u[96];
        std::snprintf(u, sizeof(u), " %d:%.9g:%.9g", e->type, double(e->x), double(e->y));
        positions << u;
      }
      // Then the shots: x:y:vx:vy.
      positions << " |";
      for (const Bullet& b : g.bullets) {
        if (b.removed) continue;
        char u[128];
        std::snprintf(u, sizeof(u), " %.9g:%.9g:%.9g:%.9g", double(b.x), double(b.y), double(b.vx), double(b.vy));
        positions << u;
      }
      // Then the geoms (type 0x22): x:y:vx:vy.
      positions << " |";
      for (const auto& e : g.world->entities) {
        if (e->type != 0x22 || (e->flags & kEntityRemoved)) continue;
        char u[128];
        std::snprintf(u, sizeof(u), " %.9g:%.9g:%.9g:%.9g", double(e->x), double(e->y), double(e->vx), double(e->vy));
        positions << u;
      }
      positions << '\n';
    }
    if (trace.is_open()) {
      const PlayerMotion& m = g.ships[0].motion;
      char line[256];
      std::snprintf(line, sizeof(line), "%d,%.9g,%.9g,%.9g,%.9g,%.9g,%d,%08x,%d,%.9g,%d,%zu,", i + 1,
                    double(g.elapsed), double(m.x), double(m.y), double(m.vx), double(m.vy), g.rng.index(),
                    g.rng.word(0), g.sequence.phase, double(g.sequence.wait), g.world->enemy_count,
                    g.world->particles.size());
      trace << line;
      // The units by type code, as memread.py unitsteps writes them.
      std::map<int, int> types;
      for (const auto& e : g.world->entities)
        if (e->unit_based && !(e->flags & kEntityRemoved)) ++types[e->type];
      bool first = true;
      for (const auto& [t, n] : types) {
        trace << (first ? "" : ";") << t << ':' << n;
        first = false;
      }
      trace << ',' << g.world->records[0].score << ',' << g.world->records[0].kills << ','
            << g.world->records[0].multiplier << '\n';
    }
  }
  if (record.is_open()) SDL_Log("recorded %d frames of %ux%u to %s", std::max(0, frames - std::max(record_from, 1) + 1),
                                w, h, record_path.string().c_str());
  UiCanvas ui;
  fe.draw(ui);
  if (!out.empty() && capture(dev, r, g, ui, out, w, h, exact_720p)) return 1;
  SDL_Log("wrote %s after %d steps: player at (%.3f, %.3f), heading %.4f, camera (%.3f, %.3f) zoom %.3f, %zu bullets, "
          "%zu entities, score %lld x%d, %zu particles, %zu debris, %zu linger quads, %d deaths, %d bombs left, "
          "%d bomb kills, %d lives%s; sequence phase %d level %d",
          out.string().c_str(), frames, g.ships[0].motion.x, g.ships[0].motion.y, g.ships[0].motion.heading,
          g.camera.x, g.camera.y, g.camera.zoom, g.bullets.size(), g.world->entities.size(),
          (long long)g.world->records[0].score, g.world->records[0].multiplier, g.world->particles.size(),
          g.world->debris.size(), g.linger_lines.body.size() / 4, g.recs[0].deaths, g.recs[0].bombs,
          g.world->bomb_kills, g.recs[0].lives, g.over ? ", game over" : "", g.sequence.phase, g.sequence.level);
  if (sound.loaded()) {
    write_wav(audio_path, samples);
    double sum[6] = {};
    float peak[6] = {};
    for (size_t k = 0; k < samples.size(); ++k) {
      sum[k % 6] += double(samples[k]) * samples[k];
      peak[k % 6] = std::max(peak[k % 6], std::fabs(samples[k]));
    }
    const double n = double(samples.size() / 6);
    SDL_Log("audio: %s, RMS / peak FL %.4f/%.3f FR %.4f/%.3f C %.4f/%.3f LFE %.4f/%.3f BL %.4f/%.3f BR %.4f/%.3f",
            audio_path.string().c_str(), std::sqrt(sum[0] / n), peak[0], std::sqrt(sum[1] / n), peak[1],
            std::sqrt(sum[2] / n), peak[2], std::sqrt(sum[3] / n), peak[3], std::sqrt(sum[4] / n), peak[4],
            std::sqrt(sum[5] / n), peak[5]);
  }
  if (g.multiplayer) {
    for (int k = 0; k < g.ship_count; ++k) {
      const Ship& s = g.ships[size_t(k)];
      SDL_Log("ship %d: pads %d/%d, number %d, record %d, colour %d, at (%.3f, %.3f)%s", k,
              g.joined_pad[size_t(s.pilot)], g.joined_pad[size_t(s.copilot)], s.number, s.record, s.colour,
              s.motion.x, s.motion.y, s.life.alive() ? "" : ", out");
    }
    for (int rec = 0; rec < g.record_count; ++rec)
      SDL_Log("record %d: score %lld x%d, %d lives, rank %d%s", rec, (long long)g.world->records[size_t(rec)].score,
              g.world->records[size_t(rec)].multiplier, g.recs[size_t(rec)].lives, g.recs[size_t(rec)].rank,
              g.recs[size_t(rec)].tie ? " (tie)" : "");
    for (int pad = 0; pad < 4; ++pad)
      if (g.lobby[size_t(pad)].joined)
        SDL_Log("pad %d: rank %d, +%d, total %d", pad, g.lobby[size_t(pad)].last_rank, g.lobby[size_t(pad)].last_points,
                g.lobby[size_t(pad)].total);
  }
  return 0;
}

// --online-preview: the online scores in a fixed state, for captures of
// their screens (no server): "unpaired", "editing" (typing the address),
// "waiting" (a pairing code), "failed", "paired", "boards", "modes" (the
// mode select's tiles with the group's boards, every mode unlocked) or
// "results" (the game over screen after a game of Deadline).
struct PreviewOnline : OnlineMenu {
  std::string state;
  explicit PreviewOnline(std::string s) : state(std::move(s)) {}
  bool paired() const override {
    return state == "paired" || state == "modes" || state == "results" || state.rfind("boards", 0) == 0;
  }
  std::string server() const override { return "https://geow2-scores.example.workers.dev"; }
  std::string player() const override { return "Ace"; }
  std::string group() const override { return "Friday Night Grid"; }
  std::string status() const override { return "last synced at 2026-10-19 23:25"; }
  std::string set_server(const std::string&) override { return ""; }
  void start_pairing() override {}
  Pairing pairing() const override {
    Pairing p;
    if (state == "failed") {
      p.state = Pairing::kFailed;
      p.error = "server not found";
    } else {
      p.state = Pairing::kWaiting;
      p.code = "KQ7M-3XPH";
      p.url = "https://geow2-scores.example.workers.dev/pair/KQ7M3XPH";
    }
    return p;
  }
  void cancel_pairing() override {}
  void unpair() override {}
  void sync_now() override {}
  std::string player_id() const override { return "p-ace"; }
  std::array<std::vector<GroupRow>, 6> boards() const override {
    std::array<std::vector<GroupRow>, 6> b;
    b[0] = {{"p-kim", "Kim", 1840250, 9, 0}, {"p-ace", "Ace", 1392405, 8, 0}, {"p-jo", "Jo", 905110, 6, 0},
            {"p-rob", "Robbie", 411500, 4, 0}};
    if (state == "modes") {
      // A long board (Ace low on it), and one Ace has no score on.
      b[1] = {{"p-kim", "Kim", 3514865, 9, 0},    {"p-jo", "Jo", 2821060, 8, 0},       {"p-rob", "Robbie", 2795535, 7, 0},
              {"p-mae", "Maettes", 2003265, 6, 0}, {"p-cj", "EagleOne CJ", 1697605, 5, 0}, {"p-sam", "Sam", 1155000, 4, 0},
              {"p-ace", "Ace", 880400, 3, 0},      {"p-lee", "Lee", 412000, 2, 0}};
      b[4] = {{"p-jo", "Jo", 9245275, 9, 0}, {"p-kim", "Kim", 6436725, 7, 0}};
    }
    return b;
  }
};

int run_frontend_headless(SDL_GPUDevice* dev, Renderer& r, Game& g, int frames, const std::vector<Press>& presses,
                          const fs::path& out, uint32_t w, uint32_t h, const std::string& online_preview) {
  Profile profile;
  if (online_preview == "modes") {
    profile.unlocked = 0x3F;
    profile.progress.fill(1);
  }
  if (online_preview == "results") {
    // A finished game of Deadline by the preview's player.
    g.rules.mode = 0;
    g.tables[0].insert(1276745, 8, "Ace");
  }
  std::array<LocalBoard, 6> boards;
  Frontend fe(g, g.text, profile, boards, {});
  PreviewOnline preview(online_preview);
  if (!online_preview.empty()) fe.set_online(&preview);
  fe.boot();
  for (int i = 0; i < frames; ++i) {
    // Pad 0 and every pad a press names are connected throughout.
    std::array<PadState, 4> pads{};
    pads[0].connected = true;
    for (const Press& p : presses) pads[size_t(p.pad)].connected = true;
    for (const Press& p : presses) {
      if (p.frame != i) continue;
      PadState& ps = pads[size_t(p.pad)];
      switch (p.axis) {
        case -1: ps.axis[5] = -1.0f; break;
        case -2: ps.axis[5] = 1.0f; break;
        case -3: ps.axis[4] = -1.0f; break;
        case -4: ps.axis[4] = 1.0f; break;
        default: ps.axis[size_t(p.axis)] = 1.0f; break;
      }
    }
    fe.frame(pads);
    // The Online scores screens once the intro is over.
    if (!online_preview.empty() && i == frames / 2) fe.preview_online(online_preview);
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev);
    tick(g, {}, r, cmd);
    submit_headless(dev, cmd, i);
  }
  UiCanvas ui;
  fe.draw(ui);
  if (!fe.qr_text().empty()) r.set_ui_texture(kTexQr, qr_image(fe.qr_text()));
  r.tuning.bloom_level = g.bloom_level;
  if (capture(dev, r, g, ui, out, w, h)) return 1;
  SDL_Log("wrote %s after %d frames: flow state %d, %zu UI quads", out.string().c_str(), frames, g.flow,
          ui.lines.body.size() / 4);
  return 0;
}

// The display settings (display.txt in the profile folder): one
// "name value" per line.
struct Display {
  bool fullscreen = false;
  bool exact_720p = false;  // render at 1280 x 720 and scale to the window
  bool vsync = true;
};

Display load_display(const fs::path& dir) {
  Display d;
  std::ifstream f(dir / "display.txt");
  std::string name;
  int value = 0;
  while (f >> name >> value) {
    if (name == "fullscreen") d.fullscreen = value != 0;
    else if (name == "exact720p") d.exact_720p = value != 0;
    else if (name == "vsync") d.vsync = value != 0;
  }
  return d;
}

void save_display(const fs::path& dir, const Display& d) {
  std::ostringstream f;
  f << "fullscreen " << int(d.fullscreen) << "\nexact720p " << int(d.exact_720p) << "\nvsync " << int(d.vsync) << '\n';
  save_text(dir / "display.txt", f.str());
}

// A Retina display's full resolution on macOS (elsewhere as before: the
// window's own pixel size).
#ifdef __APPLE__
constexpr SDL_WindowFlags kHighDensity = SDL_WINDOW_HIGH_PIXEL_DENSITY;
#else
constexpr SDL_WindowFlags kHighDensity = 0;
#endif

int run_window(SDL_GPUDevice* dev, Renderer& r, Game& g, const fs::path& data_dir, const fs::path& profile_dir,
               Display display) {
  SDL_Window* window =
      SDL_CreateWindow("geow2", 1280, 720, SDL_WINDOW_RESIZABLE | kHighDensity | (display.fullscreen ? SDL_WINDOW_FULLSCREEN : 0));
  if (!window || !SDL_ClaimWindowForGPUDevice(dev, window)) {
    SDL_Log("cannot open the window: %s", SDL_GetError());
    return 1;
  }
#if !defined(_WIN32) && !defined(__APPLE__)
  // The window's icon from geow2.png beside the shaders (Windows takes the
  // executable's icon resource, macOS the bundle's).
  try {
    const Bytes png = read_file(resource_dir() / "geow2.png");
    if (const std::optional<Image> icon = decode_png(png)) {
      if (SDL_Surface* s = SDL_CreateSurfaceFrom(int(icon->width), int(icon->height), SDL_PIXELFORMAT_RGBA32,
                                                 const_cast<uint8_t*>(icon->rgba.data()), int(icon->width * 4))) {
        SDL_SetWindowIcon(window, s);
        SDL_DestroySurface(s);
      }
    }
  } catch (const std::exception&) {
  }
#endif
  // Vsync (the default), or immediate presentation when the driver has it.
  if (!display.vsync &&
      SDL_WindowSupportsGPUPresentMode(dev, window, SDL_GPU_PRESENTMODE_IMMEDIATE))
    SDL_SetGPUSwapchainParameters(dev, window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, SDL_GPU_PRESENTMODE_IMMEDIATE);
  // The exact 720p render target, scaled into the window.
  SDL_GPUTexture* frame720 = nullptr;
  Profile profile = load_profile(profile_dir);
  const std::string device = device_id(profile_dir);
  ScoreLog score_log = load_score_log(profile_dir, device);
  if (!fs::exists(profile_dir / "scores.log")) save_score_log(profile_dir, score_log);
  std::array<LocalBoard, 6> boards;
  boards_from(score_log, boards);
  bool running = true;
  Frontend::Hooks hooks;
  hooks.save_profile = [&]() { save_profile(profile_dir, profile); };
  hooks.save_boards = [&]() { save_boards(profile_dir, boards); };
  // The online scores: a sync after each game (once the game's unlock
  // progress is in the profile, later in the same frame), when the game
  // starts, and every five minutes in the menus.
  Online online(profile_dir);
  bool sync_soon = online.paired();
  uint64_t last_sync = 0;
  hooks.scored = [&](int mode, int64_t score, int multiplier, const std::string& name) {
    score_log.add({new_event_id(), mode, score, multiplier, int64_t(std::time(nullptr)), device, name});
    save_score_log(profile_dir, score_log);
    sync_soon = true;
  };
  struct OnlineAdapter : OnlineMenu {
    Online& o;
    std::function<void()> sync;
    OnlineAdapter(Online& on, std::function<void()> s) : o(on), sync(std::move(s)) {}
    bool paired() const override { return o.paired(); }
    std::string server() const override { return o.state().server; }
    std::string player() const override { return o.state().player_name; }
    std::string group() const override { return o.state().group; }
    std::string status() const override { return o.status(); }
    std::string set_server(const std::string& typed) override {
      if (typed.empty()) return "";
      const std::string base = server_base(typed);
      if (base.empty()) return "That isn't a web address. It looks like geow2-scores.example.workers.dev.";
      o.set_server(base);
      return "";
    }
    void start_pairing() override { o.start_pairing(o.state().server, Online::device_name()); }
    Pairing pairing() const override {
      const Online::PairingView v = o.pairing();
      Pairing p;
      p.state = Pairing::State(int(v.state));
      p.code = v.code;
      p.url = v.url;
      p.error = v.error;
      return p;
    }
    void cancel_pairing() override { o.cancel_pairing(); }
    void unpair() override { o.unpair(); }
    void sync_now() override { sync(); }
    std::string player_id() const override { return o.state().player_id; }
    std::array<std::vector<GroupRow>, 6> boards() const override { return o.group_boards(); }
  };
  OnlineAdapter online_menu(online, [&]() { sync_soon = true; });
  hooks.quit = [&]() { running = false; };
  // The sound: the AOBJ graphs over the extracted waves, mixed in 5.1.
  audio::AudioEngine sound;
  std::string sound_error;
  bool has_sound = sound.init(data_dir, &sound_error);
  if (!has_sound) SDL_Log("no sound: %s", sound_error.c_str());
  audio::AudioDevice output;
  // Without an output nothing would drain the engine's message queue (a
  // device lost later is replaced by SDL, which keeps pulling).
  if (has_sound && !output.open(sound)) has_sound = false;
  audio::GameAudio glue(sound);
  if (has_sound) {
    glue.start();
    attach_sound(g, glue, []() { return uint32_t(SDL_GetTicks()); });
  }
  Frontend fe(g, g.text, profile, boards, hooks);
  fe.set_online(&online_menu);
  g.achieved = load_achievements(profile_dir);
  g.unlocked = [&](uint32_t) { save_achievements(profile_dir, g.achieved); };
  // A finished sync: the player's games from other devices, and their
  // unlocks and achievements, into this profile.
  auto apply_online = [&]() {
    OnlineProgress progress = online_progress(profile, g.achieved);
    uint32_t achieved = g.achieved;
    const auto a = online.update(score_log, progress, achieved);
    if (!a) return;
    if (a->new_games) {
      save_score_log(profile_dir, score_log);
      boards_from(score_log, boards);
      save_boards(profile_dir, boards);
    }
    if (a->progress) {
      apply_online_progress(profile, progress);
      save_profile(profile_dir, profile);
    }
    if (achieved != g.achieved) {
      g.achieved = achieved;
      save_achievements(profile_dir, g.achieved);
    }
  };
  bool text_input = false, keyboard_quiet = false;
  std::string qr_shown;  // the pairing QR code in texture kTexQr
  fe.boot();
  const SDL_GPUTextureFormat format = SDL_GetGPUSwapchainTextureFormat(dev, window);
  std::array<SDL_Gamepad*, 4> pads{};
  UiCanvas ui;
  uint64_t last = SDL_GetTicksNS();
  double pending = 0.0;
  // Pacing. The original takes one step per vblank. With vsync on a
  // display at 60 Hz or a whole multiple of it (59.94, 120, 240, ...) the
  // steps lock to the refresh: one every `frames_per_step` presented
  // frames, so frame-time jitter never shows as a doubled or missing step.
  // Other rates (or vsync off) step from real time, with frame times
  // snapped to the refresh period first.
  double refresh = 0.0;
  int frames_per_step = 0;
  int frame_in_step = 0;
  auto read_refresh = [&]() {
    refresh = 0.0;
    frames_per_step = 0;
    if (const SDL_DisplayMode* m = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window)))
      refresh = double(m->refresh_rate);
    if (display.vsync && refresh > 0.0) {
      const int k = int(std::lround(refresh / 60.0));
      if (k >= 1 && std::fabs(refresh / k - 60.0) < 0.6) frames_per_step = k;
    }
  };
  read_refresh();
  // The last frame reached the swapchain. A minimised or hidden window can
  // hand back no image without waiting for a vblank, so the refresh lock
  // only counts presented frames; the others step from real time.
  bool presented = true;
  // The system UI stand-in (Frontend::set_system_ui): minimised, hidden,
  // covered, or the focus elsewhere.
  bool minimised = false, unfocused = false;
  // A slot whose pad was pulled counts as gone for one step, though the
  // keyboard (slot 0) or a spare pad taking the slot keeps it connected.
  std::array<bool, 4> pulled{};
  auto open_pads = [&]() {
    int n = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&n);
    for (int i = 0; ids && i < n; ++i) {
      if (SDL_GetGamepadFromID(ids[i])) continue;  // already open
      for (auto& p : pads)
        if (!p) {
          p = SDL_OpenGamepad(ids[i]);
          break;
        }
    }
    SDL_free(ids);
  };
  while (running) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_EVENT_QUIT) running = false;
      switch (e.type) {
        case SDL_EVENT_WINDOW_MINIMIZED:
        case SDL_EVENT_WINDOW_HIDDEN:
        case SDL_EVENT_WINDOW_OCCLUDED:
          minimised = true;
          break;
        case SDL_EVENT_WINDOW_RESTORED:
        case SDL_EVENT_WINDOW_MAXIMIZED:
        case SDL_EVENT_WINDOW_SHOWN:
        case SDL_EVENT_WINDOW_EXPOSED:
          minimised = false;
          break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
          unfocused = true;
          break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
          unfocused = false;
          break;
        default:
          break;
      }
      if (e.type == SDL_EVENT_WINDOW_DISPLAY_CHANGED || e.type == SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED)
        read_refresh();
      if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat &&
          (e.key.scancode == SDL_SCANCODE_F11 ||
           (e.key.scancode == SDL_SCANCODE_RETURN && (e.key.mod & SDL_KMOD_ALT)) ||
           // macOS's own shortcut, Ctrl+Cmd+F
           (e.key.scancode == SDL_SCANCODE_F && (e.key.mod & SDL_KMOD_CTRL) && (e.key.mod & SDL_KMOD_GUI)))) {
        display.fullscreen = !display.fullscreen;
        SDL_SetWindowFullscreen(window, display.fullscreen);
        save_display(profile_dir, display);
        read_refresh();
      }
      // Typing into a text field (the Online scores screen's address).
      if (fe.text_entry()) {
        if (e.type == SDL_EVENT_TEXT_INPUT) fe.type(e.text.text);
        if (e.type == SDL_EVENT_KEY_DOWN) {
          if (e.key.key == SDLK_BACKSPACE) fe.text_key(Frontend::TextKey::kBackspace);
          if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) fe.text_key(Frontend::TextKey::kEnter);
          if (e.key.key == SDLK_ESCAPE) fe.text_key(Frontend::TextKey::kEscape);
          // Ctrl+V, or Cmd+V on a Mac.
          if (e.key.key == SDLK_V && (e.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI))) {
            if (char* clip = SDL_GetClipboardText()) {
              fe.paste(clip);
              SDL_free(clip);
            }
          }
        }
      }
      if (e.type == SDL_EVENT_GAMEPAD_ADDED) open_pads();
      if (e.type == SDL_EVENT_GAMEPAD_REMOVED) {
        for (size_t i = 0; i < pads.size(); ++i)
          if (pads[i] && SDL_GetGamepadID(pads[i]) == e.gdevice.which) {
            SDL_CloseGamepad(pads[i]);
            pads[i] = nullptr;
            pulled[i] = true;
          }
        open_pads();  // a fifth pad takes the freed slot
      }
    }
    fe.set_system_ui(minimised || unfocused);
    // (Before the clock is read: a frame without a command buffer loses no
    // steps, the next one catches up.)
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev);
    if (!cmd) {
      SDL_Log("no GPU command buffer: %s", SDL_GetError());
      SDL_Delay(4);
      continue;
    }
    // Fixed 1/60 s frames from real time: the front end, then the
    // simulation step (the main loop's order, sub_82037E00).
    const uint64_t now = SDL_GetTicksNS();
    double dt = double(now - last) / 1e9;
    last = now;
    int steps = 0;
    if (frames_per_step > 0 && presented) {
      // Locked to the refresh (a frame that misses a vblank slows the
      // game, as on the console).
      if (++frame_in_step >= frames_per_step) {
        frame_in_step = 0;
        steps = 1;
      }
    } else {
      if (refresh > 0.0) {
        // Snap to whole refresh periods when close.
        const double period = 1.0 / refresh;
        const double n = std::round(dt / period);
        if (n >= 1.0 && std::fabs(dt - n * period) < 0.25 * period) dt = n * period;
      }
      pending += dt;
      while (pending >= kStep && steps < 8) {
        pending -= kStep;
        ++steps;
      }
      if (pending > kStep) pending = 0.0;
    }
    for (int n = 0; n < steps; ++n) {
      PROFILE("step+audio");
      std::array<PadState, 4> state{};
      for (size_t i = 0; i < 4; ++i) state[i] = pad_state(pads[i]);
      // The keyboard is a pad except while it types, and until the keys
      // that ended the typing are let go (else Enter or Escape would act
      // on the menu too).
      if (fe.text_entry()) keyboard_quiet = true;
      if (keyboard_quiet) {
        const bool* k = SDL_GetKeyboardState(nullptr);
        keyboard_quiet = fe.text_entry() || k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_KP_ENTER] || k[SDL_SCANCODE_ESCAPE];
      }
      if (!keyboard_quiet) keyboard_pad(state[0], g.flow == 2);
      else state[0].connected = true;
      for (size_t i = 0; i < 4; ++i)
        if (pulled[i]) {
          state[i].connected = false;
          pulled[i] = false;
        }
      fe.frame(state);
      // Each joined index's pad (one profile's stick settings for all).
      std::array<Sticks, 4> s{};
      if (g.flow == 2)
        for (int j = 0; j < g.joined_count && j < 4; ++j)
          s[size_t(j)] = sticks_from(state[size_t(g.joined_pad[size_t(j)] & 3)], profile);
      tick(g, std::span<const Sticks>(s.data(), size_t(std::max(g.joined_count, 1))), r, cmd);
      if (has_sound) sound_step(g, glue, sound, profile.sound_volume, profile.music_volume);
    }
    // The online scores, between frames.
    if (fe.text_entry() != text_input) {
      text_input = fe.text_entry();
      if (text_input) SDL_StartTextInput(window);
      else SDL_StopTextInput(window);
    }
    apply_online();
    if (online.paired() && g.flow != 2 && SDL_GetTicks() - last_sync > 5 * 60 * 1000) sync_soon = true;
    if (sync_soon && online.paired() && !online.syncing()) {
      online.sync(score_log, online_progress(profile, g.achieved));
      sync_soon = false;
      last_sync = SDL_GetTicks();
    }
    PROFILE("ui+draw+present");
    ui.clear();
    fe.draw(ui);
    if (fe.qr_text() != qr_shown) {
      qr_shown = fe.qr_text();
      if (!qr_shown.empty()) r.set_ui_texture(kTexQr, qr_image(qr_shown));
    }
    r.tuning.bloom_level = g.bloom_level;
    SDL_GPUTexture* swap = nullptr;
    uint32_t w = 0, h = 0;
    bool acquired;
    {
      PROFILE("swapchain wait");
      acquired = SDL_WaitAndAcquireGPUSwapchainTexture(cmd, window, &swap, &w, &h);
    }
    presented = acquired && swap;
    if (presented) draw_frame(dev, r, cmd, swap, format, w, h, g, ui, display.exact_720p, frame720);
    SDL_SubmitGPUCommandBuffer(cmd);
    if (!presented) SDL_Delay(4);  // nothing waited for a vblank: do not spin
    if (g_profile.on) {
      // --perf in the window: a report every 600 presented frames, and
      // every frame with more or fewer than one step.
      if (steps > 1 || dt > 0.025)
        std::printf("frame with %d steps, %.2f ms since the last\n", steps, dt * 1000.0);
      g_profile.end_frame(-1, 1e9);
      static int frames_shown = 0;
      if (++frames_shown % 600 == 0) g_profile.report(refresh > 0.0 ? "window" : "window (refresh unknown)");
    }
  }
  if (frame720) SDL_ReleaseGPUTexture(dev, frame720);
  for (auto& p : pads)
    if (p) SDL_CloseGamepad(p);
  SDL_ReleaseWindowFromGPUDevice(dev, window);
  SDL_DestroyWindow(window);
  return 0;
}

// A language code (en, de, fr, es, it) to the game's number; 0 for others.
int language_code(const std::string& code) {
  const std::string c = code.substr(0, 2);
  if (c == "en") return 1;
  if (c == "de") return 3;
  if (c == "fr") return 4;
  if (c == "es") return 5;
  if (c == "it") return 6;
  return 0;
}

int button_axis(const std::string& b) {
  if (b == "a") return 0xC;
  if (b == "b") return 0xD;
  if (b == "x") return 0xE;
  if (b == "y") return 0xF;
  if (b == "start") return 0x10;
  if (b == "back") return 0x11;
  if (b == "up") return -1;
  if (b == "down") return -2;
  if (b == "left") return -3;
  if (b == "right") return -4;
  return 0xC;
}

}  // namespace

int main(int argc, char** argv) {
  attach_parent_console();
  // The name, version and app id (Wayland's, matching the desktop entry's
  // name and StartupWMClass, and the Flatpak's id).
  SDL_SetAppMetadata("geow2", GEOW2_VERSION, "com.geow2.geow2");
  // The data folder: --data, else "data" next to the program (next to
  // geow2.app on macOS), else "data" in the user folder.
  fs::path data_dir = fs::path(SDL_GetBasePath() ? SDL_GetBasePath() : "") / "data";
  bool data_given = false;
  fs::path profile_dir;
  int frames = -1;
  Sticks sticks;
  std::array<std::optional<Sticks>, 4> pad_sticks{};
  std::array<LobbyPad, 4> lobby{};
  bool versus = false, multiplayer = false;
  fs::path screenshot;
  fs::path trace_path, rng_path, dump_path, inputs_path, positions_path, audio_path, record_path;
  int record_from = 1;
  SetupDev setup_dev;
  fs::path merge_from;
  std::string online_pair;
  std::string online_preview;
  bool online_sync = false;
  int rng_step = -1, dump_step = -1;
  uint32_t shot_w = 1280, shot_h = 720;
  int64_t seed = -1;
  int64_t crt_state = -1;  // --crt-state: the C runtime rand()'s state at the start, in hex
  int bomb_step = -1;
  int mode = 0;
  int start_level = -1;
  bool frontend = false;
  int fullscreen = -1, exact_720p = -1, vsync = -1;  // -1: as saved
  int language = 0;  // 0: the system's (windowed) or English (headless)
  std::vector<DevSpawn> spawns;
  std::vector<Press> presses;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--data" && i + 1 < argc) {
      data_dir = argv[++i];
      data_given = true;
    } else if (a == "--profile" && i + 1 < argc) profile_dir = argv[++i];
    else if (a == "--frames" && i + 1 < argc) frames = std::atoi(argv[++i]);
    else if (a == "--screenshot" && i + 1 < argc) screenshot = argv[++i];
    else if (a == "--seed" && i + 1 < argc) seed = std::atoll(argv[++i]);
    else if (a == "--mode" && i + 1 < argc) mode = std::clamp(std::atoi(argv[++i]), 0, 5);
    else if (a == "--level" && i + 1 < argc) start_level = std::clamp(std::atoi(argv[++i]), 0, 19);
    else if (a == "--frontend") frontend = true;
    else if (a == "--trace" && i + 1 < argc) trace_path = argv[++i];
    else if (a == "--inputs" && i + 1 < argc) inputs_path = argv[++i];
    else if (a == "--emulator-rounding") emulator_rounding = true;
    else if (a == "--perf") g_profile.on = true;
    else if (a == "--crt-state" && i + 1 < argc) crt_state = int64_t(std::strtoul(argv[++i], nullptr, 16));
    else if (a == "--positions" && i + 1 < argc) positions_path = argv[++i];
    else if (a == "--audio-out" && i + 1 < argc) audio_path = argv[++i];
    else if (a == "--merge-profile" && i + 1 < argc) merge_from = argv[++i];
    else if (a == "--online-pair" && i + 1 < argc) online_pair = argv[++i];
    else if (a == "--online-sync") online_sync = true;
    else if (a == "--online-preview" && i + 1 < argc) online_preview = argv[++i];
    else if (a == "--setup-preview" && i + 1 < argc) setup_dev.preview = argv[++i];
    else if (a == "--setup-from" && i + 1 < argc) setup_dev.source = argv[++i];
    else if (a == "--record" && i + 2 < argc) {
      record_path = argv[++i];
      record_from = std::atoi(argv[++i]);
    }
    else if (a == "--rng-state" && i + 2 < argc) {
      rng_path = argv[++i];
      rng_step = std::atoi(argv[++i]);
    } else if (a == "--dump" && i + 2 < argc) {
      dump_step = std::atoi(argv[++i]);
      dump_path = argv[++i];
    }
    else if (a == "--language" && i + 1 < argc) language = language_code(argv[++i]);
    else if (a == "--fullscreen" || a == "--windowed") fullscreen = a == "--fullscreen";
    else if (a == "--720p" || a == "--native") exact_720p = a == "--720p";
    else if (a == "--vsync" || a == "--no-vsync") vsync = a == "--vsync";
    else if (a == "--press" && i + 2 < argc) {
      // <button>[:<pad>]
      const int f = std::atoi(argv[++i]);
      const std::string b = argv[++i];
      const size_t colon = b.find(':');
      const int pad = colon == std::string::npos ? 0 : std::clamp(std::atoi(b.c_str() + colon + 1), 0, 3);
      presses.push_back({f, button_axis(b.substr(0, colon)), pad});
    } else if (a == "--size" && i + 2 < argc) {
      // Up to 8K (the capture's download buffer is w x h x 4 bytes).
      shot_w = uint32_t(std::clamp(std::atoi(argv[++i]), 1, 7680));
      shot_h = uint32_t(std::clamp(std::atoi(argv[++i]), 1, 4320));
    } else if (a == "--stick" && i + 2 < argc) {
      sticks.move_x = clamp16(std::atoi(argv[++i]));
      sticks.move_y = clamp16(std::atoi(argv[++i]));
    } else if (a == "--spawn" && i + 3 < argc) {
      const int type = std::atoi(argv[++i]);
      const float x = float(std::atof(argv[++i]));
      const float y = float(std::atof(argv[++i]));
      spawns.push_back({type, x, y});
    } else if (a == "--bomb" && i + 1 < argc) {
      bomb_step = std::atoi(argv[++i]);
    } else if (a == "--trigger" && i + 1 < argc) {
      // The right trigger held at this raw value (0-255) on every step.
      sticks.right_trigger = uint8_t(std::clamp(std::atoi(argv[++i]), 0, 255));
    } else if (a == "--fire" && i + 2 < argc) {
      sticks.fire_x = clamp16(std::atoi(argv[++i]));
      sticks.fire_y = clamp16(std::atoi(argv[++i]));
    } else if (a == "--player" && i + 3 < argc) {
      const int pad = std::clamp(std::atoi(argv[++i]), 0, 3);
      lobby[size_t(pad)].joined = true;
      lobby[size_t(pad)].team = std::clamp(std::atoi(argv[++i]), 0, 3);
      lobby[size_t(pad)].role = std::clamp(std::atoi(argv[++i]), 0, 2);
      multiplayer = true;
    } else if (a == "--versus") {
      versus = true;
    } else if ((a == "--pad-stick" || a == "--pad-fire") && i + 3 < argc) {
      const int pad = std::clamp(std::atoi(argv[++i]), 0, 3);
      Sticks& p = pad_sticks[size_t(pad)] ? *pad_sticks[size_t(pad)] : pad_sticks[size_t(pad)].emplace(sticks);
      const int16_t x = clamp16(std::atoi(argv[++i])), y = clamp16(std::atoi(argv[++i]));
      if (a == "--pad-stick") {
        p.move_x = x;
        p.move_y = y;
      } else {
        p.fire_x = x;
        p.fire_y = y;
      }
    } else if (a == "--version") {
      std::printf("geow2 %s\n", GEOW2_VERSION);
      return 0;
    } else {
      // --help and --help-dev print to stdout and succeed; anything else
      // unknown is an error.
      const bool help = a == "--help" || a == "-h", dev = a == "--help-dev";
      FILE* out = help || dev ? stdout : stderr;
      std::fprintf(out,
                   "geow2 %s\n"
                   "usage: geow2 [--data <data-dir>] [--profile <dir>] [--fullscreen | --windowed]\n"
                   "             [--720p | --native] [--vsync | --no-vsync] [--language en|de|fr|es|it]\n"
                   "             [--version] [--help-dev]\n"
                   "       geow2 [--profile <dir>] --merge-profile <another device's profile folder>\n"
                   "       geow2 [--profile <dir>] --online-pair <score server> | --online-sync\n",
                   GEOW2_VERSION);
      if (dev)
        std::fprintf(out,
                     "development (headless runs; see the comment at the top of src/app/main.cpp):\n"
                     "  geow2 --frames <n> (--screenshot <png> | --audio-out <wav>) [--size <w> <h>] [--seed <n>]\n"
                     "        [--mode <0-5>] [--level <0-19>] [--stick <x> <y>] [--fire <x> <y>] [--bomb <step>]\n"
                     "        [--spawn <type> <x> <y>]... [--player <pad> <team> <role>]... [--versus]\n"
                     "        [--pad-stick <pad> <x> <y>] [--pad-fire <pad> <x> <y>] [--inputs <csv>]\n"
                     "        [--rng-state <file> <step>] [--crt-state <hex>] [--emulator-rounding]\n"
                     "        [--trace <csv>] [--positions <file>] [--dump <step> <file>] [--perf]\n"
                     "        [--record <raw-file> <first-step>]\n"
                     "  geow2 --frames <n> --screenshot <png> --frontend [--press <frame> <button>]...\n"
                     "        [--online-preview unpaired|editing|waiting|failed|paired|boards|modes|results]\n"
                     "  geow2 [--data <dir>] --setup-preview <png> | --setup-from <package>\n");
      return help || dev ? 0 : 2;
    }
  }
  // --frames with --screenshot, --audio-out or --record runs without a window.
  const bool headless = frames >= 0 && (!screenshot.empty() || !audio_path.empty() || !record_path.empty());
  // The GPU device needs the video subsystem even when no window is opened.
  if (!SDL_Init(headless ? SDL_INIT_VIDEO : (SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))) {
    SDL_Log("SDL_Init: %s", SDL_GetError());
    return 1;
  }
  // Sound on its own, so that a machine without an audio device still
  // plays (silently).
  if (!headless && !SDL_InitSubSystem(SDL_INIT_AUDIO)) SDL_Log("no audio: %s", SDL_GetError());
  if (profile_dir.empty()) {
    char* pref = SDL_GetPrefPath("geow2", "geow2");
    profile_dir = pref ? fs::path(pref) : fs::path("profile");
    SDL_free(pref);
  }
  if (!merge_from.empty()) {
    const int rc = merge_profile(profile_dir, merge_from);
    SDL_Quit();
    return rc;
  }
  if (!online_pair.empty() || online_sync) {
    const int rc = online_command(profile_dir, online_pair);
    SDL_Quit();
    return rc;
  }
  Game game;
  // The language: --language, else the system's first preferred one the
  // game has (the console language in the original), English when headless.
  if (language == 0 && !headless) {
    int n = 0;
    if (SDL_Locale** locales = SDL_GetPreferredLocales(&n)) {
      for (int k = 0; k < n && language == 0; ++k)
        if (locales[k] && locales[k]->language) language = language_code(locales[k]->language);
      SDL_free(locales);
    }
  }
  if (!data_given && !fs::exists(data_dir)) {
    // geow2.app/Contents/Resources/ is the base path in a bundle: the folder
    // holding the app comes first.
    const fs::path base = fs::path(SDL_GetBasePath() ? SDL_GetBasePath() : "").parent_path();
    const fs::path app = base.parent_path().parent_path();
    if (app.extension() == ".app" && fs::exists(app.parent_path() / "data")) data_dir = app.parent_path() / "data";
  }
  if (!data_given && !fs::exists(data_dir)) {
    char* pref = SDL_GetPrefPath("geow2", "geow2");
    if (pref && fs::exists(fs::path(pref) / "data")) data_dir = fs::path(pref) / "data";
    SDL_free(pref);
  }
  // No data folder anywhere: the setup window makes one from the player's
  // copy of the game, in the user folder (or where --data says).
  // --setup-preview and --setup-from show it regardless.
  if ((!headless && !fs::exists(data_dir / "files" / "Data.bin")) || !setup_dev.preview.empty() ||
      !setup_dev.source.empty()) {
    fs::path target = data_dir;
    if (!data_given) {
      char* pref = SDL_GetPrefPath("geow2", "geow2");
      target = (pref ? fs::path(pref) : profile_dir) / "data";
      SDL_free(pref);
    }
    if (!run_setup(target, setup_dev) || !setup_dev.preview.empty()) {
      SDL_Quit();
      return 0;
    }
    data_dir = target;
  }
  if (!load(game, data_dir, seed, mode, language == 0 ? 1 : language)) {
    // Started from a desktop there is no console to read the log from.
    if (!headless) {
      const std::string msg = "The game's data was not found in\n" + data_dir.string() +
                              "\n\nCreate it once from your copy of the game:\n"
                              "  geow2-extract unpack <your LIVE package or extracted folder> \"" +
                              data_dir.string() + "\"\n\nor start the game with --data <folder>.";
      SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "geow2", msg.c_str(), nullptr);
    }
    return 1;
  }
  // Level.StartLevel (value 0x8219D458) for development: Sequence starts at
  // that level (sub_82039628 sets both the level and the one shown).
  // (After a multiplayer start, which resets Sequence.)
  if (multiplayer) {
    game.lobby = lobby;
    start_multiplayer(game, mode, versus);
  }
  if (start_level >= 0) game.sequence.level = game.sequence.shown_level = start_level;
  // Vulkan with SPIR-V, or Metal (macOS) with the shaders translated to MSL.
  SDL_GPUDevice* dev = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL, false, nullptr);
  if (!dev) {
    SDL_Log("no GPU device: %s", SDL_GetError());
    if (!headless) {
      const std::string msg = std::string("No usable graphics device (the game needs Vulkan, or Metal on macOS):\n") + SDL_GetError();
      SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "geow2", msg.c_str(), nullptr);
    }
    return 1;
  }
  Renderer::error_boxes = !headless;
  Renderer renderer;
  if (!renderer.init(dev, resource_dir() / "shaders")) return 1;
  renderer.load_ui_textures(data_dir / "files");
  for (size_t i = 0; i < game.achievements.size() && i < 12; ++i)
    renderer.set_ui_texture(kTexAchievement0 + int(i), game.achievements[i].icon);
  int rc;
  if (headless && frontend) {
    rc = run_frontend_headless(dev, renderer, game, frames, presses, screenshot, shot_w, shot_h, online_preview);
  } else if (headless) {
    std::array<Sticks, 4> per_pad{};
    for (size_t p = 0; p < 4; ++p) per_pad[p] = pad_sticks[p] ? *pad_sticks[p] : sticks;
    const StepInputs inputs = inputs_path.empty() ? StepInputs{} : load_inputs(inputs_path);
    if (crt_state >= 0) game.world->crt.set_state(uint32_t(crt_state));
    rc = run_headless(dev, renderer, game, frames, per_pad, inputs, bomb_step, spawns, screenshot, shot_w, shot_h,
                      exact_720p == 1, trace_path, rng_path, rng_step, dump_path, dump_step, positions_path, data_dir,
                      audio_path, record_path, record_from);
    SDL_Log("crt state %08x", game.world->crt.state());
  } else {
    Display d = load_display(profile_dir);
    if (fullscreen >= 0) d.fullscreen = fullscreen != 0;
    if (exact_720p >= 0) d.exact_720p = exact_720p != 0;
    if (vsync >= 0) d.vsync = vsync != 0;
    if (fullscreen >= 0 || exact_720p >= 0 || vsync >= 0) save_display(profile_dir, d);
    rc = run_window(dev, renderer, game, data_dir, profile_dir, d);
  }
  renderer.release();
  SDL_DestroyGPUDevice(dev);
  SDL_Quit();
  return rc;
}

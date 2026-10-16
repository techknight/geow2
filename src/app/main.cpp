// geow2: the game.
//
//   geow2 [--data <cache-dir>] [--profile <dir>] [--fullscreen | --windowed] [--720p | --native]
//         [--vsync | --no-vsync] [--language en|de|fr|es|it]
//   geow2 --data <cache-dir> --frames <n> --screenshot <out.png> [--size <w> <h>] [--mode <n>] [--level <n>]
//         [--stick <x> <y>] [--fire <x> <y>] [--bomb <step>] [--trigger <0-255>] [--spawn <type> <x> <y>]...
//         [--player <pad> <team> <role>]... [--versus] [--pad-stick <pad> <x> <y>] [--pad-fire <pad> <x> <y>]
//         [--inputs <csv>]
//   geow2 --data <cache-dir> --frames <n> --screenshot <out.png> --frontend [--press <frame> <button>]...
//
// The first form opens the window at the front end; the profile, the
// local score boards and the display settings (display.txt: fullscreen,
// exact 720p render, vsync; F11 toggles fullscreen) are kept in --profile
// (default: the user's preferences folder). The second runs n simulation steps of a game of
// --mode (0 Deadline, 1 Evolved, 2 Sequence, 3 Waves, 4 Pacifism, 5 King)
// without a window, with a fixed raw stick value (XInput convention),
// --bomb pulling the trigger on one step and --spawn adding enemies (type
// 16: a power-up item) at the
// start, and saves the final frame. --player joins a pad to a multiplayer
// game (team 0-3, role 0 alone, 1 pilot, 2 copilot; --versus for teams
// against each other), every pad holding the --stick and --fire values
// unless --pad-stick / --pad-fire set its own. --inputs replays the pads
// step by step from a CSV with a step column and raw lx, ly, rx, ry, lt,
// rt columns for pad 0 and p1_lx .. p3_rt for the others (a step without
// a row keeps the previous one; a pad without columns keeps its
// --stick / --fire; --crt-state <hex> sets the C runtime rand()'s state,
// which the original keeps for the whole session, and the run prints its
// final value; --emulator-rounding rounds the fused multiply-adds as
// Xenia does (game/fused.h), for comparing with games recorded there; a
// <rng-state>-spawner.txt next to the --rng-state
// file loads the spawner state with it); --positions writes every unit after each step. With
// a seq_phase column (Sequence's phase in the
// recorded game) the music's TransitionSwitch, which the original reads
// from its audio thread, is raised on the steps where that phase went
// from 1 to 2 or from 5 to 6. The third runs the front end instead,
// pressing a button (a, b, x, y, start, back, up, down, left, right; pad 0,
// or another with :<pad>) on the given frames; it never saves.

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "app/frontend.h"
#include "game/fused.h"
#include "audio/device.h"
#include "audio/engine.h"
#include "audio/game_audio.h"
#include "app/game.h"
#include "app/png.h"
#include "app/renderer.h"
#include "game/gravity_well.h"
#include "game/spawn.h"

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

// The profile block and the local boards on disk.
Profile load_profile(const fs::path& dir) {
  std::array<uint8_t, 0x30> b{};
  std::ifstream f(dir / "profile.bin", std::ios::binary);
  if (!f || !f.read(reinterpret_cast<char*>(b.data()), std::streamsize(b.size()))) return Profile{};
  return Profile::from_bytes(b);
}

void save_profile(const fs::path& dir, const Profile& p) {
  std::error_code ec;
  fs::create_directories(dir, ec);
  const auto b = p.to_bytes();
  std::ofstream f(dir / "profile.bin", std::ios::binary | std::ios::trunc);
  f.write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
}

// One row per line: mode, score, name (tab-separated).
void load_boards(const fs::path& dir, std::array<LocalBoard, 6>& boards) {
  std::ifstream f(dir / "scores.txt");
  std::string line;
  while (std::getline(f, line)) {
    std::istringstream s(line);
    std::string mode, score, name;
    if (!std::getline(s, mode, '\t') || !std::getline(s, score, '\t')) continue;
    std::getline(s, name);
    const int m = std::atoi(mode.c_str());
    if (m < 0 || m > 5) continue;
    auto& rows = boards[size_t(m)].rows;
    if (rows.size() < LocalBoard::kRows) rows.push_back({std::atoll(score.c_str()), name});
  }
  for (auto& b : boards)
    std::stable_sort(b.rows.begin(), b.rows.end(), [](const auto& a, const auto& c) { return a.score > c.score; });
}

void save_boards(const fs::path& dir, const std::array<LocalBoard, 6>& boards) {
  std::error_code ec;
  fs::create_directories(dir, ec);
  std::ofstream f(dir / "scores.txt", std::ios::trunc);
  for (int m = 0; m < 6; ++m)
    for (const auto& r : boards[size_t(m)].rows) f << m << '\t' << r.score << '\t' << r.name << '\n';
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

int capture(SDL_GPUDevice* dev, Renderer& r, const Game& g, const UiCanvas& ui, const fs::path& out, uint32_t w,
            uint32_t h, bool exact_720p = false) {
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
      out.phase[size_t(step)] = std::atoi(v[size_t(cph)].c_str());
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

int run_headless(SDL_GPUDevice* dev, Renderer& r, Game& g, int frames, const std::array<Sticks, 4>& sticks,
                 const StepInputs& inputs, int bomb_step, const std::vector<DevSpawn>& spawns, const fs::path& out, uint32_t w, uint32_t h,
                 bool exact_720p, const fs::path& trace_path, const fs::path& rng_path, int rng_step,
                 const fs::path& dump_path, int dump_step, const fs::path& positions_path) {
  // --trace: after each step, the clock and slot 0's object (x, y, vx,
  // vy) at full precision, as the oracle's memory reads give them.
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
    tick(g, std::span<const Sticks>(s.data(), size_t(std::max(g.joined_count, 1))), r, cmd);
    SDL_SubmitGPUCommandBuffer(cmd);
    // --rng-state: the generator's state read from the oracle (the index,
    // then 624 words, in hex), loaded after that step.
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
  Profile profile;
  std::array<LocalBoard, 6> boards;
  Frontend fe(g, g.text, profile, boards, {});
  fe.watch_game();
  UiCanvas ui;
  fe.draw(ui);
  if (capture(dev, r, g, ui, out, w, h, exact_720p)) return 1;
  SDL_Log("wrote %s after %d steps: player at (%.3f, %.3f), heading %.4f, camera (%.3f, %.3f) zoom %.3f, %zu bullets, "
          "%zu entities, score %lld x%d, %zu particles, %zu debris, %zu linger quads, %d deaths, %d bombs left, "
          "%d bomb kills, %d lives%s; sequence phase %d level %d",
          out.string().c_str(), frames, g.ships[0].motion.x, g.ships[0].motion.y, g.ships[0].motion.heading,
          g.camera.x, g.camera.y, g.camera.zoom, g.bullets.size(), g.world->entities.size(),
          (long long)g.world->records[0].score, g.world->records[0].multiplier, g.world->particles.size(),
          g.world->debris.size(), g.linger_lines.body.size() / 4, g.recs[0].deaths, g.recs[0].bombs,
          g.world->bomb_kills, g.recs[0].lives, g.over ? ", game over" : "", g.sequence.phase, g.sequence.level);
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

int run_frontend_headless(SDL_GPUDevice* dev, Renderer& r, Game& g, int frames, const std::vector<Press>& presses,
                          const fs::path& out, uint32_t w, uint32_t h) {
  Profile profile;
  std::array<LocalBoard, 6> boards;
  Frontend fe(g, g.text, profile, boards, {});
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
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev);
    tick(g, {}, r, cmd);
    SDL_SubmitGPUCommandBuffer(cmd);
  }
  UiCanvas ui;
  fe.draw(ui);
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
  std::error_code ec;
  fs::create_directories(dir, ec);
  std::ofstream f(dir / "display.txt", std::ios::trunc);
  f << "fullscreen " << int(d.fullscreen) << "\nexact720p " << int(d.exact_720p) << "\nvsync " << int(d.vsync) << '\n';
}

int run_window(SDL_GPUDevice* dev, Renderer& r, Game& g, const fs::path& cache, const fs::path& profile_dir,
               Display display) {
  SDL_Window* window =
      SDL_CreateWindow("geow2", 1280, 720, SDL_WINDOW_RESIZABLE | (display.fullscreen ? SDL_WINDOW_FULLSCREEN : 0));
  if (!window || !SDL_ClaimWindowForGPUDevice(dev, window)) {
    SDL_Log("cannot open the window: %s", SDL_GetError());
    return 1;
  }
  // Vsync (the default), or immediate presentation when the driver has it.
  if (!display.vsync &&
      SDL_WindowSupportsGPUPresentMode(dev, window, SDL_GPU_PRESENTMODE_IMMEDIATE))
    SDL_SetGPUSwapchainParameters(dev, window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, SDL_GPU_PRESENTMODE_IMMEDIATE);
  // The exact 720p render target, scaled into the window.
  SDL_GPUTexture* frame720 = nullptr;
  Profile profile = load_profile(profile_dir);
  std::array<LocalBoard, 6> boards;
  load_boards(profile_dir, boards);
  bool running = true;
  Frontend::Hooks hooks;
  hooks.save_profile = [&]() { save_profile(profile_dir, profile); };
  hooks.save_boards = [&]() { save_boards(profile_dir, boards); };
  hooks.quit = [&]() { running = false; };
  // The sound: the AOBJ graphs over the extracted waves, mixed in 5.1.
  audio::AudioEngine sound;
  std::string sound_error;
  const bool has_sound = sound.init(cache, &sound_error);
  if (!has_sound) SDL_Log("no sound: %s", sound_error.c_str());
  audio::AudioDevice output;
  if (has_sound) output.open(sound);
  audio::GameAudio glue(sound);
  if (has_sound) {
    glue.start();
    // The events behind rate gates or in alternating pairs go through the
    // glue with the millisecond clock.
    g.world->sound = [&](audio::Event e) {
      const uint32_t now = uint32_t(SDL_GetTicks());
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
    g.world->repulsar_hit = [&]() { glue.repulsar_hit_wall(uint32_t(SDL_GetTicks())); };
    g.world->well_hum = [&](const Entity* w, int hits) { glue.gravity_well_hits(reinterpret_cast<uintptr_t>(w), hits); };
    g.music = [&](bool in_game) { glue.select_music(in_game, g.rules.mode, frozen(g)); };
  }
  Frontend fe(g, g.text, profile, boards, hooks);
  // The unlocked achievements, kept beside the profile.
  {
    std::ifstream f(profile_dir / "achievements.txt");
    unsigned long bits = 0;
    if (f >> std::hex >> bits) g.achieved = uint32_t(bits);
  }
  g.unlocked = [&](uint32_t) {
    std::error_code ec;
    fs::create_directories(profile_dir, ec);
    std::ofstream f(profile_dir / "achievements.txt", std::ios::trunc);
    f << std::hex << g.achieved << '\n';
  };
  fe.boot();
  const SDL_GPUTextureFormat format = SDL_GetGPUSwapchainTextureFormat(dev, window);
  std::array<SDL_Gamepad*, 4> pads{};
  UiCanvas ui;
  uint64_t last = SDL_GetTicksNS();
  double pending = 0.0;
  while (running) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_EVENT_QUIT) running = false;
      if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat &&
          (e.key.scancode == SDL_SCANCODE_F11 ||
           (e.key.scancode == SDL_SCANCODE_RETURN && (e.key.mod & SDL_KMOD_ALT)))) {
        display.fullscreen = !display.fullscreen;
        SDL_SetWindowFullscreen(window, display.fullscreen);
        save_display(profile_dir, display);
      }
      if (e.type == SDL_EVENT_GAMEPAD_ADDED) {
        for (auto& p : pads)
          if (!p) {
            p = SDL_OpenGamepad(e.gdevice.which);
            break;
          }
      }
      if (e.type == SDL_EVENT_GAMEPAD_REMOVED) {
        for (auto& p : pads)
          if (p && SDL_GetGamepadID(p) == e.gdevice.which) {
            SDL_CloseGamepad(p);
            p = nullptr;
          }
      }
    }
    // Fixed 1/60 s frames from real time: the front end, then the
    // simulation step (the main loop's order, sub_82037E00).
    const uint64_t now = SDL_GetTicksNS();
    pending += double(now - last) / 1e9;
    last = now;
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev);
    for (int n = 0; pending >= kStep && n < 8; ++n) {
      std::array<PadState, 4> state{};
      for (size_t i = 0; i < 4; ++i) state[i] = pad_state(pads[i]);
      keyboard_pad(state[0], g.flow == 2);
      fe.frame(state);
      // Each joined index's pad (one profile's stick settings for all).
      std::array<Sticks, 4> s{};
      if (g.flow == 2)
        for (int j = 0; j < g.joined_count && j < 4; ++j)
          s[size_t(j)] = sticks_from(state[size_t(g.joined_pad[size_t(j)] & 3)], profile);
      tick(g, std::span<const Sticks>(s.data(), size_t(std::max(g.joined_count, 1))), r, cmd);
      if (has_sound) {
        // sub_82078970 after the wells, then sub_82079BC0.
        if (!frozen(g)) glue.gravity_wells_update(kWellMaxHits);
        const GameAudioState a = audio_state(g);
        audio::AudioFrame f;
        f.paused = a.paused;
        f.sfx_option = profile.sound_volume;
        f.music_option = profile.music_volume;
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
      pending -= kStep;
    }
    if (pending > kStep) pending = 0.0;
    ui.clear();
    fe.draw(ui);
    r.tuning.bloom_level = g.bloom_level;
    SDL_GPUTexture* swap = nullptr;
    uint32_t w = 0, h = 0;
    if (SDL_WaitAndAcquireGPUSwapchainTexture(cmd, window, &swap, &w, &h) && swap)
      draw_frame(dev, r, cmd, swap, format, w, h, g, ui, display.exact_720p, frame720);
    SDL_SubmitGPUCommandBuffer(cmd);
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
  fs::path cache = fs::path(SDL_GetBasePath() ? SDL_GetBasePath() : "") / "cache";
  fs::path profile_dir;
  int frames = -1;
  Sticks sticks;
  std::array<std::optional<Sticks>, 4> pad_sticks{};
  std::array<LobbyPad, 4> lobby{};
  bool versus = false, multiplayer = false;
  fs::path screenshot;
  fs::path trace_path, rng_path, dump_path, inputs_path, positions_path;
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
    if (a == "--data" && i + 1 < argc) cache = argv[++i];
    else if (a == "--profile" && i + 1 < argc) profile_dir = argv[++i];
    else if (a == "--frames" && i + 1 < argc) frames = std::atoi(argv[++i]);
    else if (a == "--screenshot" && i + 1 < argc) screenshot = argv[++i];
    else if (a == "--seed" && i + 1 < argc) seed = std::atoll(argv[++i]);
    else if (a == "--mode" && i + 1 < argc) mode = std::clamp(std::atoi(argv[++i]), 0, 5);
    else if (a == "--level" && i + 1 < argc) start_level = std::clamp(std::atoi(argv[++i]), 0, 19);
    else if (a == "--frontend") frontend = true;
    else if (a == "--trace" && i + 1 < argc) trace_path = argv[++i];
    else if (a == "--inputs" && i + 1 < argc) inputs_path = argv[++i];
    else if (a == "--emulator-rounding") emulator_rounding = true;
    else if (a == "--crt-state" && i + 1 < argc) crt_state = int64_t(std::strtoul(argv[++i], nullptr, 16));
    else if (a == "--positions" && i + 1 < argc) positions_path = argv[++i];
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
      shot_w = uint32_t(std::atoi(argv[++i]));
      shot_h = uint32_t(std::atoi(argv[++i]));
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
    } else {
      std::fprintf(stderr,
                   "usage: geow2 [--data <cache>] [--profile <dir>] [--fullscreen|--windowed] [--720p|--native]"
                   " [--vsync|--no-vsync] [--language <en|de|fr|es|it>] [--seed <n>] [--frames <n> --screenshot <png>"
                   " [--size <w> <h>] [--mode <0-5>] [--level <0-19>] [--stick <x> <y>] [--fire <x> <y>]"
                   " [--bomb <step>] [--spawn <type> <x> <y>]... [--player <pad> <team> <role>]... [--versus]"
                   " [--pad-stick <pad> <x> <y>] [--pad-fire <pad> <x> <y>] [--inputs <csv>] [--emulator-rounding] [--crt-state <hex>]"
                   " | --frontend [--press <frame> <button>]...]\n");
      return 2;
    }
  }
  const bool headless = frames >= 0 && !screenshot.empty();
  // The GPU device needs the video subsystem even when no window is opened.
  if (!SDL_Init(headless ? SDL_INIT_VIDEO : (SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))) {
    SDL_Log("SDL_Init: %s", SDL_GetError());
    return 1;
  }
  if (profile_dir.empty()) {
    char* pref = SDL_GetPrefPath("geow2", "geow2");
    profile_dir = pref ? fs::path(pref) : fs::path("profile");
    SDL_free(pref);
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
  if (!load(game, cache, seed, mode, language == 0 ? 1 : language)) return 1;
  // Level.StartLevel (value 0x8219D458) for development: Sequence starts at
  // that level (sub_82039628 sets both the level and the one shown).
  if (start_level >= 0) game.sequence.level = game.sequence.shown_level = start_level;
  if (multiplayer) {
    game.lobby = lobby;
    start_multiplayer(game, mode, versus);
  }
  SDL_GPUDevice* dev = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, false, nullptr);
  if (!dev) {
    SDL_Log("no GPU device: %s", SDL_GetError());
    return 1;
  }
  Renderer renderer;
  if (!renderer.init(dev, fs::path(SDL_GetBasePath() ? SDL_GetBasePath() : "") / "shaders")) return 1;
  renderer.load_ui_textures(cache / "files");
  for (size_t i = 0; i < game.achievements.size() && i < 12; ++i)
    renderer.set_ui_texture(kTexAchievement0 + int(i), game.achievements[i].icon);
  int rc;
  if (headless && frontend) {
    rc = run_frontend_headless(dev, renderer, game, frames, presses, screenshot, shot_w, shot_h);
  } else if (headless) {
    std::array<Sticks, 4> per_pad{};
    for (size_t p = 0; p < 4; ++p) per_pad[p] = pad_sticks[p] ? *pad_sticks[p] : sticks;
    const StepInputs inputs = inputs_path.empty() ? StepInputs{} : load_inputs(inputs_path);
    if (crt_state >= 0) game.world->crt.set_state(uint32_t(crt_state));
    rc = run_headless(dev, renderer, game, frames, per_pad, inputs, bomb_step, spawns, screenshot, shot_w, shot_h,
                      exact_720p == 1, trace_path, rng_path, rng_step, dump_path, dump_step, positions_path);
    SDL_Log("crt state %08x", game.world->crt.state());
  } else {
    Display d = load_display(profile_dir);
    if (fullscreen >= 0) d.fullscreen = fullscreen != 0;
    if (exact_720p >= 0) d.exact_720p = exact_720p != 0;
    if (vsync >= 0) d.vsync = vsync != 0;
    if (fullscreen >= 0 || exact_720p >= 0 || vsync >= 0) save_display(profile_dir, d);
    rc = run_window(dev, renderer, game, cache, profile_dir, d);
  }
  renderer.release();
  SDL_DestroyGPUDevice(dev);
  SDL_Quit();
  return rc;
}

// geow2: the game.
//
//   geow2 [--data <cache-dir>] [--profile <dir>]
//   geow2 --data <cache-dir> --frames <n> --screenshot <out.png> [--size <w> <h>] [--mode <n>] [--level <n>]
//         [--stick <x> <y>] [--fire <x> <y>] [--bomb <step>] [--spawn <type> <x> <y>]...
//   geow2 --data <cache-dir> --frames <n> --screenshot <out.png> --frontend [--press <frame> <button>]...
//
// The first form opens the window at the front end; the profile and the
// local score boards are kept in --profile (default: the user's
// preferences folder). The second runs n simulation steps of a game of
// --mode (0 Deadline, 1 Evolved, 2 Sequence, 3 Waves, 4 Pacifism, 5 King)
// without a window, with a fixed raw stick value (XInput convention),
// --bomb pulling the trigger on one step and --spawn adding enemies at the
// start, and saves the final frame. The third runs the front end instead,
// pressing a button (a, b, x, y, start, back, up, down, left, right) on
// the given frames; it never saves.

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "app/frontend.h"
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

int capture(SDL_GPUDevice* dev, Renderer& r, const Game& g, const UiCanvas& ui, const fs::path& out, uint32_t w,
            uint32_t h) {
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
  render(r, cmd, tex, tci.format, w, h, g, ui);
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
  return 0;
}

int run_headless(SDL_GPUDevice* dev, Renderer& r, Game& g, int frames, const Sticks& sticks, int bomb_step,
                 const std::vector<DevSpawn>& spawns, const fs::path& out, uint32_t w, uint32_t h) {
  for (const DevSpawn& d : spawns) spawn_enemy(*g.world, d.type, 0.0f, d.x, d.y);
  for (int i = 0; i < frames; ++i) {
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev);
    Sticks s = sticks;
    if (i == bomb_step) s.right_trigger = 255;
    tick(g, s, r, cmd);
    SDL_SubmitGPUCommandBuffer(cmd);
  }
  UiCanvas ui;
  draw_game_hud(g, ui.lines);
  if (capture(dev, r, g, ui, out, w, h)) return 1;
  SDL_Log("wrote %s after %d steps: player at (%.3f, %.3f), heading %.4f, camera (%.3f, %.3f) zoom %.3f, %zu bullets, "
          "%zu entities, score %lld x%d, %zu particles, %zu debris, %zu linger quads, %d deaths, %d bombs left, "
          "%d bomb kills, %d lives%s; sequence phase %d level %d",
          out.string().c_str(), frames, g.player.x, g.player.y, g.player.heading, g.camera.x, g.camera.y,
          g.camera.zoom, g.bullets.size(), g.world->entities.size(), (long long)g.world->score, g.world->multiplier,
          g.world->particles.size(), g.world->debris.size(), g.linger_lines.body.size() / 4, g.life.deaths, g.life.bombs,
          g.world->bomb_kills, g.life.lives, g.over ? ", game over" : "", g.sequence.phase, g.sequence.level);
  return 0;
}

int run_frontend_headless(SDL_GPUDevice* dev, Renderer& r, Game& g, int frames, const std::vector<Press>& presses,
                          const fs::path& out, uint32_t w, uint32_t h) {
  Profile profile;
  std::array<LocalBoard, 6> boards;
  Frontend fe(g, g.text, profile, boards, {});
  fe.boot();
  for (int i = 0; i < frames; ++i) {
    std::array<PadState, 4> pads{};
    pads[0].connected = true;
    for (const Press& p : presses) {
      if (p.frame != i) continue;
      switch (p.axis) {
        case -1: pads[0].axis[5] = -1.0f; break;
        case -2: pads[0].axis[5] = 1.0f; break;
        case -3: pads[0].axis[4] = -1.0f; break;
        case -4: pads[0].axis[4] = 1.0f; break;
        default: pads[0].axis[size_t(p.axis)] = 1.0f; break;
      }
    }
    fe.frame(pads);
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev);
    tick(g, Sticks{}, r, cmd);
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

int run_window(SDL_GPUDevice* dev, Renderer& r, Game& g, const fs::path& cache, const fs::path& profile_dir) {
  SDL_Window* window = SDL_CreateWindow("geow2", 1280, 720, SDL_WINDOW_RESIZABLE);
  if (!window || !SDL_ClaimWindowForGPUDevice(dev, window)) {
    SDL_Log("cannot open the window: %s", SDL_GetError());
    return 1;
  }
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
      const Sticks s = (g.flow == 2) ? sticks_from(state[0], profile) : Sticks{};
      tick(g, s, r, cmd);
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
      render(r, cmd, swap, format, w, h, g, ui);
    SDL_SubmitGPUCommandBuffer(cmd);
  }
  for (auto& p : pads)
    if (p) SDL_CloseGamepad(p);
  SDL_ReleaseWindowFromGPUDevice(dev, window);
  SDL_DestroyWindow(window);
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
  fs::path screenshot;
  uint32_t shot_w = 1280, shot_h = 720;
  int64_t seed = -1;
  int bomb_step = -1;
  int mode = 0;
  int start_level = -1;
  bool frontend = false;
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
    else if (a == "--press" && i + 2 < argc) {
      const int f = std::atoi(argv[++i]);
      presses.push_back({f, button_axis(argv[++i])});
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
    } else if (a == "--fire" && i + 2 < argc) {
      sticks.fire_x = clamp16(std::atoi(argv[++i]));
      sticks.fire_y = clamp16(std::atoi(argv[++i]));
    } else {
      std::fprintf(stderr,
                   "usage: geow2 [--data <cache>] [--profile <dir>] [--seed <n>] [--frames <n> --screenshot <png>"
                   " [--size <w> <h>] [--mode <0-5>] [--level <0-19>] [--stick <x> <y>] [--fire <x> <y>]"
                   " [--bomb <step>] [--spawn <type> <x> <y>]... | --frontend [--press <frame> <button>]...]\n");
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
  if (!load(game, cache, seed, mode)) return 1;
  // Level.StartLevel (0x8219D438) for development: Sequence starts at
  // that level (sub_82039628 sets both the level and the one shown).
  if (start_level >= 0) game.sequence.level = game.sequence.shown_level = start_level;
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
  if (headless && frontend)
    rc = run_frontend_headless(dev, renderer, game, frames, presses, screenshot, shot_w, shot_h);
  else if (headless)
    rc = run_headless(dev, renderer, game, frames, sticks, bomb_step, spawns, screenshot, shot_w, shot_h);
  else
    rc = run_window(dev, renderer, game, cache, profile_dir);
  renderer.release();
  SDL_DestroyGPUDevice(dev);
  SDL_Quit();
  return rc;
}

// geow2: the game.
//
//   geow2 [--data <cache-dir>] [--mode <n>]
//   geow2 --data <cache-dir> [--mode <n>] --frames <n> --screenshot <out.png> [--size <w> <h>] [--stick <x> <y>]
//         [--fire <x> <y>] [--bomb <step>] [--spawn <type> <x> <y>]...
//
// The second form runs n simulation steps without a window (input: a fixed
// raw stick value, XInput convention; --bomb pulls the trigger on one
// step; --spawn adds an enemy of a spawn type at the start, for
// development captures) and saves the final frame. --mode picks the game
// mode until the front end is ported: 0 Deadline (the default), 1 Evolved,
// 2 Sequence, 3 Waves, 4 Pacifism, 5 King.
//
// Early development build: Deadline's arena with the player (shots,
// shield, bombs, death and respawn) and its wanderers, drawn as the
// original's glowing line quads with its bloom.

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "app/game.h"
#include "app/png.h"
#include "app/renderer.h"
#include "game/spawn.h"

namespace fs = std::filesystem;
using namespace geow2;

namespace {

struct DevSpawn {
  int type;
  float x, y;
};

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
  render(r, cmd, tex, tci.format, w, h, g);
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
  SDL_Log("wrote %s after %d steps: player at (%.3f, %.3f), heading %.4f, camera (%.3f, %.3f) zoom %.3f, %zu bullets, "
          "%zu entities, score %lld x%d, %zu particles, %zu debris, %zu linger quads, %d deaths, %d bombs left, "
          "%d bomb kills, %d lives%s; sequence phase %d level %d",
          out.string().c_str(), frames, g.player.x, g.player.y, g.player.heading, g.camera.x, g.camera.y,
          g.camera.zoom, g.bullets.size(), g.world->entities.size(), (long long)g.world->score, g.world->multiplier, g.world->particles.size(),
          g.world->debris.size(), g.linger_lines.body.size() / 4, g.life.deaths, g.life.bombs, g.world->bomb_kills, g.life.lives,
          g.over ? ", game over" : "", g.sequence.phase, g.sequence.level);
  SDL_ReleaseGPUTransferBuffer(dev, down);
  SDL_ReleaseGPUTexture(dev, tex);
  return 0;
}

int16_t clamp16(int v) { return int16_t(std::clamp(v, -32768, 32767)); }

int run_window(SDL_GPUDevice* dev, Renderer& r, Game& g) {
  SDL_Window* window = SDL_CreateWindow("geow2", 1280, 720, SDL_WINDOW_RESIZABLE);
  if (!window || !SDL_ClaimWindowForGPUDevice(dev, window)) {
    SDL_Log("cannot open the window: %s", SDL_GetError());
    return 1;
  }
  const SDL_GPUTextureFormat format = SDL_GetGPUSwapchainTextureFormat(dev, window);
  SDL_Gamepad* pad = nullptr;
  uint64_t last = SDL_GetTicksNS();
  double pending = 0.0;
  for (bool running = true; running;) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_EVENT_QUIT) running = false;
      if (e.type == SDL_EVENT_GAMEPAD_ADDED && !pad) pad = SDL_OpenGamepad(e.gdevice.which);
      if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) running = false;
    }
    // Sticks in the XInput convention (SDL's Y axes point down): left
    // moves, right fires (the original's default, as its rules screens show).
    int mx = 0, my = 0, fx = 0, fy = 0, lt = 0, rt = 0;
    if (pad) {
      mx = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX);
      my = -int(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY));
      fx = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTX);
      fy = -int(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY));
      // SDL's triggers run 0..32767; XInput's are bytes.
      lt = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) * 255 / 32767;
      rt = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) * 255 / 32767;
    }
    const bool* keys = SDL_GetKeyboardState(nullptr);
    if (keys[SDL_SCANCODE_A]) mx = -32768;
    if (keys[SDL_SCANCODE_D]) mx = 32767;
    if (keys[SDL_SCANCODE_W]) my = 32767;
    if (keys[SDL_SCANCODE_S]) my = -32768;
    if (keys[SDL_SCANCODE_LEFT]) fx = -32768;
    if (keys[SDL_SCANCODE_RIGHT]) fx = 32767;
    if (keys[SDL_SCANCODE_UP]) fy = 32767;
    if (keys[SDL_SCANCODE_DOWN]) fy = -32768;
    if (keys[SDL_SCANCODE_SPACE]) rt = 255;
    const Sticks sticks{clamp16(mx), clamp16(my), clamp16(fx), clamp16(fy), uint8_t(std::clamp(lt, 0, 255)),
                        uint8_t(std::clamp(rt, 0, 255))};

    // Fixed 1/60 s steps from real time; the picture shows the latest step.
    const uint64_t now = SDL_GetTicksNS();
    pending += double(now - last) / 1e9;
    last = now;
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev);
    for (int n = 0; pending >= kStep && n < 8; ++n) {
      tick(g, sticks, r, cmd);
      pending -= kStep;
    }
    if (pending > kStep) pending = 0.0;

    SDL_GPUTexture* swap = nullptr;
    uint32_t w = 0, h = 0;
    if (SDL_WaitAndAcquireGPUSwapchainTexture(cmd, window, &swap, &w, &h) && swap) render(r, cmd, swap, format, w, h, g);
    SDL_SubmitGPUCommandBuffer(cmd);
  }
  SDL_ReleaseWindowFromGPUDevice(dev, window);
  SDL_DestroyWindow(window);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  fs::path cache = fs::path(SDL_GetBasePath() ? SDL_GetBasePath() : "") / "cache";
  int frames = -1;
  Sticks sticks;
  fs::path screenshot;
  uint32_t shot_w = 1280, shot_h = 720;
  int64_t seed = -1;
  int bomb_step = -1;
  int mode = 0;
  int start_level = -1;
  std::vector<DevSpawn> spawns;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--data" && i + 1 < argc) cache = argv[++i];
    else if (a == "--frames" && i + 1 < argc) frames = std::atoi(argv[++i]);
    else if (a == "--screenshot" && i + 1 < argc) screenshot = argv[++i];
    else if (a == "--seed" && i + 1 < argc) seed = std::atoll(argv[++i]);
    else if (a == "--mode" && i + 1 < argc) mode = std::clamp(std::atoi(argv[++i]), 0, 5);
    else if (a == "--level" && i + 1 < argc) start_level = std::clamp(std::atoi(argv[++i]), 0, 19);
    else if (a == "--size" && i + 2 < argc) {
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
                   "usage: geow2 [--data <cache>] [--seed <n>] [--mode <0-5>] [--level <0-19>] [--frames <n> --screenshot <png> [--size <w> <h>]"
                   " [--stick <x> <y>] [--fire <x> <y>] [--bomb <step>] [--spawn <type> <x> <y>]...]\n");
      return 2;
    }
  }
  const bool headless = frames >= 0 && !screenshot.empty();
  // The GPU device needs the video subsystem even when no window is opened.
  if (!SDL_Init(headless ? SDL_INIT_VIDEO : (SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))) {
    SDL_Log("SDL_Init: %s", SDL_GetError());
    return 1;
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
  const int rc =
      headless ? run_headless(dev, renderer, game, frames, sticks, bomb_step, spawns, screenshot, shot_w, shot_h) : run_window(dev, renderer, game);
  renderer.release();
  SDL_DestroyGPUDevice(dev);
  SDL_Quit();
  return rc;
}

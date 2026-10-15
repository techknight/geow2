// geow2: the game.
//
//   geow2 [--data <cache-dir>]
//   geow2 --data <cache-dir> --frames <n> --screenshot <out.png> [--size <w> <h>] [--stick <x> <y>]
//         [--fire <x> <y>]
//
// The second form runs n simulation steps without a window (input: a fixed
// raw stick value, XInput convention) and saves the final frame.
//
// Early development build: the player ship and its shots in the arena,
// drawn as the original's glowing line quads with its bloom.

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "app/png.h"
#include "app/renderer.h"
#include "data/flask.h"
#include "extract/bytes.h"
#include "game/bullets.h"
#include "game/camera.h"
#include "game/game_math.h"
#include "game/grid.h"
#include "game/light_map.h"
#include "game/line_quads.h"
#include "game/model_geometry.h"
#include "game/player_motion.h"
#include "game/rng.h"
#include "game/stars.h"
#include "game/rotation.h"

namespace fs = std::filesystem;
using namespace geow2;

namespace {

constexpr float kStep = 1.0f / 60.0f;          // dat_8219D280 = 1.0 / 60.0 (sub_8205D538)
constexpr float kArenaHalfX = 300.0f;          // dat_82000CF4 (sub_82036250)
constexpr float kArenaHalfY = 200.0f;          // dat_82000CF0
constexpr float kShapeCapSize = 1.0f;          // dat_820006C4 (sub_82061908)

const FlaskStruct* as_struct(const FlaskValue* v) {
  auto* p = v ? std::get_if<std::shared_ptr<FlaskStruct>>(&v->v) : nullptr;
  return p ? p->get() : nullptr;
}

float as_float(const FlaskValue* v) {
  auto* d = v ? std::get_if<double>(&v->v) : nullptr;
  return d ? float(*d) : 0.0f;
}

// The Unit record with this name (the original looks units up by name,
// sub_82031D40), its decoded value kept alive with the struct.
struct Unit {
  FlaskValue value;
  const FlaskStruct* s = nullptr;
};

std::optional<Unit> find_unit(const Flask& db, const std::string& name) {
  for (uint32_t r = 0; r < db.records().size(); ++r) {
    if (db.types()[db.records()[r].type].name != "Unit") continue;
    Unit u{db.decode(r)};
    u.s = as_struct(&u.value);
    const FlaskValue* n = u.s ? u.s->find("name") : nullptr;
    if (n && std::holds_alternative<std::string>(n->v) && std::get<std::string>(n->v) == name) return u;
  }
  return std::nullopt;
}

struct Game {
  std::unique_ptr<Flask> db;
  std::unique_ptr<GameMath> gm;
  ModelGeometry ship;
  float tint[4] = {1, 1, 1, 1};  // player colour (constructor: unit tint, rgb times alpha)
  PlayerMotion player;
  CameraState camera;
  PlayerWeapon weapon;
  std::vector<Bullet> bullets;
  BulletShape bullet_shape;
  // The player's light: unit glowColour (rgb times alpha) and glowSize
  // (sub_82050D00).
  float glow[3] = {0, 0, 0};
  float glow_size = 0.0f;
  // The background grid and what each step produces for drawing.
  Grid grid;
  GridTuning grid_tuning;
  float grid_fade = 0.0f;                // dat_821A170C
  bool grid_visible = false;
  std::vector<GridVertex> grid_lines;    // this step's line quads
  std::vector<GridVertex> warp_mesh;     // this step's light map warp
  std::vector<LightVertex> lights;       // this step's light sprites
  GameRng rng;
  StarField stars;
  std::vector<StarVertex> star_vertices;  // this step's stars
};

bool load(Game& g, const fs::path& cache) {
  try {
    g.db = std::make_unique<Flask>(read_file(cache / "files" / "Data.bin"));
    g.gm = std::make_unique<GameMath>(GameMath::from_image(read_file(cache / "xex" / "image.bin")));
  } catch (const std::exception& e) {
    SDL_Log("cannot load the cache at %s: %s (run geow2-extract unpack first)", cache.string().c_str(), e.what());
    return false;
  }
  // The player constructor (sub_82051448) starts from the unit "player"
  // (dat_82000F18); a colour index above 0 (its seventh argument, +0xA0)
  // switches the model to player1..4 by the eighth and the tint to
  // player<index>. Single player is taken to use index 0: the original's
  // ship is white there, as "player"'s tint is (oracle).
  const auto unit = find_unit(*g.db, "player");
  if (!unit) {
    SDL_Log("Data.bin has no player unit");
    return false;
  }
  if (const FlaskStruct* tint = as_struct(unit->s->find("tint"))) {
    const float a = as_float(tint->find("a"));
    g.tint[0] = as_float(tint->find("r")) * a;
    g.tint[1] = as_float(tint->find("g")) * a;
    g.tint[2] = as_float(tint->find("b")) * a;
    g.tint[3] = 1.0f;
  }
  if (const FlaskStruct* glow = as_struct(unit->s->find("glowColour"))) {
    const float a = as_float(glow->find("a"));
    g.glow[0] = a * as_float(glow->find("r"));
    g.glow[1] = as_float(glow->find("g")) * a;
    g.glow[2] = as_float(glow->find("b")) * a;
  }
  g.glow_size = as_float(unit->s->find("glowSize"));
  // The grid at rest over the arena (sub_8205B000 from sub_82036250).
  g.grid.reset(kArenaHalfX, kArenaHalfY);
  // The generator is seeded from the tick count at start-up
  // (sub_82037C20), then the starfield draws from it (sub_82075318).
  g.rng.seed_from_ticks(SDL_GetTicksNS() / 1000000);
  g.stars.init(g.rng);
  // Player constructor sub_82051448: radius +0x20 = 5.0 (dat_820006F0),
  // heading +0x14 = -pi/2 (dat_820010F4). Spawn position: the arena centre
  // for now (the spawn point argument is not traced yet).
  g.player.radius = 5.0f;
  g.player.heading = -1.57079637f;
  const FlaskValue* model_ref = unit->s->find("model");
  const auto* ref = model_ref ? std::get_if<FlaskRef>(&model_ref->v) : nullptr;
  if (!ref) {
    SDL_Log("player has no model");
    return false;
  }
  const FlaskValue model = g.db->decode(ref->record);
  if (const FlaskStruct* m = as_struct(&model)) g.ship = build_model_geometry(*m, *g.gm);
  try {
    g.bullet_shape = BulletShape::from_image(read_file(cache / "xex" / "image.bin"));
  } catch (const std::exception& e) {
    SDL_Log("cannot read the bullet shape: %s", e.what());
    return false;
  }
  return true;
}

// One simulation step with a raw (XInput convention) left stick value.
struct Sticks {
  int16_t move_x = 0, move_y = 0, fire_x = 0, fire_y = 0;  // raw, XInput convention
};

void step(Game& g, const Sticks& s) {
  // The player's update (sub_8204FF00): thrust, then firing; then the
  // common update integrates its position.
  const Vec2 move = stick_vector(stick_axis(s.move_x, false), stick_axis(s.move_y, true));
  const Vec2 fire = stick_vector(stick_axis(s.fire_x, false), stick_axis(s.fire_y, true));
  player_thrust(g.player, move, *g.gm);
  player_fire_step(g.player, g.weapon, fire, kStep, *g.gm, g.bullets);
  integrate(g.player, kStep);
  Vec2 n;
  if (arena_contact(g.player.radius, kArenaHalfX, kArenaHalfY, g.player.x, g.player.y, n)) player_wall(g.player, n);
  // Then the other entities, bullets included (the player list is updated
  // first, so bullets fired this step already move: to verify in the oracle).
  std::erase_if(g.bullets, [](Bullet& b) { return !bullet_step(b, kStep, kArenaHalfX, kArenaHalfY); });
  // The camera follows after the entity updates (sub_820366A0).
  camera_follow_single(g.camera, &g.player.x, &g.player.y, 1, kArenaHalfX, kArenaHalfY);

  // The grid (sub_8205C8F8, then sub_8205B880 on worker threads): pushes
  // gathered from the entities, the spring step, and the line pass from
  // the positions the step read. Bullets push with radius +0x8C and
  // strength 2.0 (sub_8205B578).
  GridInputs in;
  in.pushes.emplace_back();
  for (const Bullet& b : g.bullets) in.pushes.back().push_back({b.x, b.y, b.grid_radius, 2.0f});
  g.grid.step(in, kArenaHalfX, kArenaHalfY);
  grid_lines(g.grid, g.grid_tuning, g.grid_fade, g.grid_lines);
  grid_warp_mesh(g.grid, g.grid_tuning, g.warp_mesh);
  // The draw then eases the fade in (sub_8205CAF0; in game, so toward 1).
  g.grid_fade = grid_fade_step(g.grid_fade, true);
  g.grid_visible = g.grid_fade > kGridVisible;

  // Light sprites (sub_82065E38 walks the lists; bullets come before the
  // player): bullets size radius * Unit.Projectile.LightFactor (6.0),
  // colour * 8 (sub_82051DB8); the player its glow (sub_82050D00).
  g.lights.clear();
  for (const Bullet& b : g.bullets) {
    const float c[3] = {b.colour[0] * 8.0f, b.colour[1] * 8.0f, b.colour[2] * 8.0f};
    emit_light(g.lights, b.radius * 6.0f, b.x, b.y, c);
  }
  emit_light(g.lights, g.glow_size, g.player.x, g.player.y, g.glow);

  // The stars' step (sub_82075548, on the render worker each frame).
  g.stars.update(*g.gm, 1.0f, g.star_vertices);
}

// The foreground lines: the ship's body (part 0), each line coloured as
// sub_82061DA0 does (line colour times the draw colour) with the model's
// cap size, and the shots' shapes (sub_82061908) in their colour.
void foreground(const Game& g, LineBatch& out) {
  out.clear();
  if (!g.ship.parts.empty()) {
    const Mat4 world = placement_style_none(*g.gm, g.player.x, g.player.y, g.player.heading);
    for (const ModelLine& l : g.ship.parts[0]) {
      const float colour[4] = {l.r * g.tint[0], l.g * g.tint[1], l.b * g.tint[2], l.a * g.tint[3]};
      emit_line(out, kLineWidthUnit, g.ship.cap_size, mul(l.p0, world), mul(l.p1, world), colour);
    }
  }
  // The glow sprite of the shots is not drawn yet.
  for (const Bullet& b : g.bullets)
    for (const Segment& s : bullet_segments(b, g.bullet_shape, *g.gm))
      emit_line(out, kLineWidthUnit, kShapeCapSize, s.a, s.b, b.colour);
}

void render(Renderer& r, SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* target, SDL_GPUTextureFormat format,
            uint32_t w, uint32_t h, const Game& g) {
  static LineBatch front;
  foreground(g, front);
  r.draw(cmd, target, format, w, h, camera_view_projection(g.camera, *g.gm), g.star_vertices,
         g.grid_visible ? &g.grid_lines : nullptr, front);
}

// One simulation step and its light map update (one per step, as the
// original renders one frame per step).
void tick(Game& g, const Sticks& s, Renderer& r, SDL_GPUCommandBuffer* cmd) {
  step(g, s);
  r.update_light_map(cmd, g.warp_mesh, g.lights);
}

int run_headless(SDL_GPUDevice* dev, Renderer& r, Game& g, int frames, const Sticks& sticks, const fs::path& out,
                 uint32_t w, uint32_t h) {
  for (int i = 0; i < frames; ++i) {
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev);
    tick(g, sticks, r, cmd);
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
  SDL_Log("wrote %s after %d steps: player at (%.3f, %.3f), heading %.4f, camera (%.3f, %.3f) zoom %.3f, %zu bullets",
          out.string().c_str(), frames, g.player.x, g.player.y, g.player.heading, g.camera.x, g.camera.y,
          g.camera.zoom, g.bullets.size());
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
    int mx = 0, my = 0, fx = 0, fy = 0;
    if (pad) {
      mx = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX);
      my = -int(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY));
      fx = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTX);
      fy = -int(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY));
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
    const Sticks sticks{clamp16(mx), clamp16(my), clamp16(fx), clamp16(fy)};

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
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--data" && i + 1 < argc) cache = argv[++i];
    else if (a == "--frames" && i + 1 < argc) frames = std::atoi(argv[++i]);
    else if (a == "--screenshot" && i + 1 < argc) screenshot = argv[++i];
    else if (a == "--size" && i + 2 < argc) {
      shot_w = uint32_t(std::atoi(argv[++i]));
      shot_h = uint32_t(std::atoi(argv[++i]));
    } else if (a == "--stick" && i + 2 < argc) {
      sticks.move_x = clamp16(std::atoi(argv[++i]));
      sticks.move_y = clamp16(std::atoi(argv[++i]));
    } else if (a == "--fire" && i + 2 < argc) {
      sticks.fire_x = clamp16(std::atoi(argv[++i]));
      sticks.fire_y = clamp16(std::atoi(argv[++i]));
    } else {
      std::fprintf(stderr,
                   "usage: geow2 [--data <cache>] [--frames <n> --screenshot <png> [--size <w> <h>] [--stick <x> <y>]"
                   " [--fire <x> <y>]]\n");
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
  if (!load(game, cache)) return 1;
  SDL_GPUDevice* dev = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, false, nullptr);
  if (!dev) {
    SDL_Log("no GPU device: %s", SDL_GetError());
    return 1;
  }
  Renderer renderer;
  if (!renderer.init(dev, fs::path(SDL_GetBasePath() ? SDL_GetBasePath() : "") / "shaders")) return 1;
  const int rc =
      headless ? run_headless(dev, renderer, game, frames, sticks, screenshot, shot_w, shot_h) : run_window(dev, renderer, game);
  renderer.release();
  SDL_DestroyGPUDevice(dev);
  SDL_Quit();
  return rc;
}

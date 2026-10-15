// geow2: the game.
//
//   geow2 [--data <cache-dir>]
//   geow2 --data <cache-dir> --frames <n> --screenshot <out.png> [--stick <x> <y>]
//
// The second form runs n simulation steps without a window (input: a fixed
// raw stick value, XInput convention) and saves the final frame.
//
// Early development build: only the player ship in the arena. Provisional
// until their code is traced and ported: the camera is fixed at the arena
// centre (the follow code is not ported), and lines are drawn as plain
// thin lines (not the original's quads, glow and bloom).

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
#include "data/flask.h"
#include "extract/bytes.h"
#include "game/game_math.h"
#include "game/model_geometry.h"
#include "game/player_motion.h"
#include "game/rotation.h"

namespace fs = std::filesystem;
using namespace geow2;

namespace {

constexpr float kStep = 1.0f / 60.0f;          // dat_8219D280 = 1.0 / 60.0 (sub_8205D538)
constexpr float kArenaHalfX = 300.0f;          // dat_82000CF4 (sub_82036250)
constexpr float kArenaHalfY = 200.0f;          // dat_82000CF0
constexpr float kAspect = 1.77777779f;         // dat_82002DB0
constexpr float kFov = 0.0174532924f * 36.0f;  // pi/180 * 36 with zoom 1 (sub_82067A20)

struct Vertex {
  float x, y, z, w;
  float r, g, b, a;
};

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
};

bool load(Game& g, const fs::path& cache) {
  try {
    g.db = std::make_unique<Flask>(read_file(cache / "files" / "Data.bin"));
    g.gm = std::make_unique<GameMath>(GameMath::from_image(read_file(cache / "xex" / "image.bin")));
  } catch (const std::exception& e) {
    SDL_Log("cannot load the cache at %s: %s (run geow2-extract unpack first)", cache.string().c_str(), e.what());
    return false;
  }
  const auto unit = find_unit(*g.db, "player1");
  if (!unit) {
    SDL_Log("Data.bin has no player1 unit");
    return false;
  }
  if (const FlaskStruct* tint = as_struct(unit->s->find("tint"))) {
    const float a = as_float(tint->find("a"));
    g.tint[0] = as_float(tint->find("r")) * a;
    g.tint[1] = as_float(tint->find("g")) * a;
    g.tint[2] = as_float(tint->find("b")) * a;
    g.tint[3] = 1.0f;
  }
  const FlaskValue* model_ref = unit->s->find("model");
  const auto* ref = model_ref ? std::get_if<FlaskRef>(&model_ref->v) : nullptr;
  if (!ref) {
    SDL_Log("player1 has no model");
    return false;
  }
  const FlaskValue model = g.db->decode(ref->record);
  if (const FlaskStruct* m = as_struct(&model)) g.ship = build_model_geometry(*m, *g.gm);
  return true;
}

// One simulation step with a raw (XInput convention) left stick value.
void step(Game& g, int16_t raw_x, int16_t raw_y) {
  const Vec2 move = stick_vector(stick_axis(raw_x, false), stick_axis(raw_y, true));
  player_thrust(g.player, move, *g.gm);
  integrate(g.player, kStep);
  Vec2 n;
  if (arena_contact(g.player.radius, kArenaHalfX, kArenaHalfY, g.player.x, g.player.y, n)) player_wall(g.player, n);
}

// The ship's body (part 0) in world space, coloured as the draw does
// (line colour times the draw colour, sub_82061DA0).
std::vector<Vertex> ship_vertices(const Game& g) {
  std::vector<Vertex> out;
  if (g.ship.parts.empty()) return out;
  const Mat4 world = placement_style_none(*g.gm, g.player.x, g.player.y, g.player.heading);
  for (const ModelLine& l : g.ship.parts[0]) {
    const Vec4 a = mul(l.p0, world), b = mul(l.p1, world);
    const float r = l.r * g.tint[0], gg = l.g * g.tint[1], bl = l.b * g.tint[2], al = l.a * g.tint[3];
    out.push_back({a.x, a.y, a.z, 1.0f, r, gg, bl, al});
    out.push_back({b.x, b.y, b.z, 1.0f, r, gg, bl, al});
  }
  return out;
}

Mat4 view_projection(const GameMath& gm) {
  // Provisional: camera fixed above the arena centre (follow not ported).
  const Mat4 basis = look_at_basis({0, 0, 512.0f, 1}, {0, 0, -512.0f, 0}, {0, -1.0f, 0, 0});
  return mul(inverse(basis), perspective(gm, kFov, kAspect, 1.0f, 10000.0f));
}

struct Gpu {
  SDL_GPUDevice* device = nullptr;
  SDL_GPUShader* vs = nullptr;
  SDL_GPUShader* fs = nullptr;
  SDL_GPUBuffer* vertices = nullptr;
  SDL_GPUTransferBuffer* upload = nullptr;
  uint32_t capacity = 0;
};

SDL_GPUShader* load_shader(SDL_GPUDevice* dev, const fs::path& path, SDL_GPUShaderStage stage) {
  Bytes code;
  try {
    code = read_file(path);
  } catch (const std::exception&) {
    SDL_Log("missing shader %s", path.string().c_str());
    return nullptr;
  }
  SDL_GPUShaderCreateInfo info{};
  info.code_size = code.size();
  info.code = code.data();
  info.entrypoint = "main";
  info.format = SDL_GPU_SHADERFORMAT_SPIRV;
  info.stage = stage;
  info.num_uniform_buffers = stage == SDL_GPU_SHADERSTAGE_VERTEX ? 1 : 0;
  return SDL_CreateGPUShader(dev, &info);
}

SDL_GPUGraphicsPipeline* make_pipeline(const Gpu& gpu, SDL_GPUTextureFormat format) {
  // Provisional additive blend for the premultiplied line colours.
  SDL_GPUColorTargetDescription target{};
  target.format = format;
  target.blend_state.enable_blend = true;
  target.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
  target.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
  target.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
  target.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
  target.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
  target.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
  SDL_GPUVertexBufferDescription vb{};
  vb.slot = 0;
  vb.pitch = sizeof(Vertex);
  vb.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
  SDL_GPUVertexAttribute attrs[2]{};
  attrs[0] = {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 0};
  attrs[1] = {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 16};
  SDL_GPUGraphicsPipelineCreateInfo info{};
  info.vertex_shader = gpu.vs;
  info.fragment_shader = gpu.fs;
  info.vertex_input_state.vertex_buffer_descriptions = &vb;
  info.vertex_input_state.num_vertex_buffers = 1;
  info.vertex_input_state.vertex_attributes = attrs;
  info.vertex_input_state.num_vertex_attributes = 2;
  info.primitive_type = SDL_GPU_PRIMITIVETYPE_LINELIST;
  info.target_info.color_target_descriptions = &target;
  info.target_info.num_color_targets = 1;
  return SDL_CreateGPUGraphicsPipeline(gpu.device, &info);
}

void upload_vertices(Gpu& gpu, SDL_GPUCommandBuffer* cmd, const std::vector<Vertex>& v) {
  const uint32_t size = uint32_t(v.size() * sizeof(Vertex));
  if (size == 0) return;
  if (size > gpu.capacity) {
    if (gpu.vertices) SDL_ReleaseGPUBuffer(gpu.device, gpu.vertices);
    if (gpu.upload) SDL_ReleaseGPUTransferBuffer(gpu.device, gpu.upload);
    SDL_GPUBufferCreateInfo b{SDL_GPU_BUFFERUSAGE_VERTEX, size, 0};
    gpu.vertices = SDL_CreateGPUBuffer(gpu.device, &b);
    SDL_GPUTransferBufferCreateInfo t{SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, size, 0};
    gpu.upload = SDL_CreateGPUTransferBuffer(gpu.device, &t);
    gpu.capacity = size;
  }
  void* p = SDL_MapGPUTransferBuffer(gpu.device, gpu.upload, true);
  std::memcpy(p, v.data(), size);
  SDL_UnmapGPUTransferBuffer(gpu.device, gpu.upload);
  SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
  SDL_GPUTransferBufferLocation src{gpu.upload, 0};
  SDL_GPUBufferRegion dst{gpu.vertices, 0, size};
  SDL_UploadToGPUBuffer(copy, &src, &dst, true);
  SDL_EndGPUCopyPass(copy);
}

// Draws the frame into `target` (w x h), the 16:9 picture letterboxed.
void draw(Gpu& gpu, SDL_GPUCommandBuffer* cmd, SDL_GPUGraphicsPipeline* pipe, SDL_GPUTexture* target, uint32_t w,
          uint32_t h, const Game& g) {
  const std::vector<Vertex> v = ship_vertices(g);
  upload_vertices(gpu, cmd, v);
  SDL_GPUColorTargetInfo ct{};
  ct.texture = target;
  ct.clear_color = {0, 0, 0, 1};
  ct.load_op = SDL_GPU_LOADOP_CLEAR;
  ct.store_op = SDL_GPU_STOREOP_STORE;
  SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &ct, 1, nullptr);
  float vw = float(w), vh = float(w) / kAspect;
  if (vh > float(h)) {
    vh = float(h);
    vw = float(h) * kAspect;
  }
  const SDL_GPUViewport vp{(float(w) - vw) * 0.5f, (float(h) - vh) * 0.5f, vw, vh, 0.0f, 1.0f};
  SDL_SetGPUViewport(pass, &vp);
  if (!v.empty()) {
    SDL_BindGPUGraphicsPipeline(pass, pipe);
    const SDL_GPUBufferBinding bind{gpu.vertices, 0};
    SDL_BindGPUVertexBuffers(pass, 0, &bind, 1);
    const Mat4 m = view_projection(*g.gm);
    SDL_PushGPUVertexUniformData(cmd, 0, m.m, sizeof(m.m));
    SDL_DrawGPUPrimitives(pass, uint32_t(v.size()), 1, 0, 0);
  }
  SDL_EndGPURenderPass(pass);
}

int run_headless(Gpu& gpu, Game& g, int frames, int16_t sx, int16_t sy, const fs::path& out) {
  for (int i = 0; i < frames; ++i) step(g, sx, sy);
  const uint32_t w = 1280, h = 720;
  SDL_GPUTextureCreateInfo tci{};
  tci.type = SDL_GPU_TEXTURETYPE_2D;
  tci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
  tci.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
  tci.width = w;
  tci.height = h;
  tci.layer_count_or_depth = 1;
  tci.num_levels = 1;
  SDL_GPUTexture* tex = SDL_CreateGPUTexture(gpu.device, &tci);
  SDL_GPUGraphicsPipeline* pipe = make_pipeline(gpu, tci.format);
  SDL_GPUTransferBufferCreateInfo dl{SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, w * h * 4, 0};
  SDL_GPUTransferBuffer* down = SDL_CreateGPUTransferBuffer(gpu.device, &dl);
  if (!tex || !pipe || !down) {
    SDL_Log("cannot create the offscreen target: %s", SDL_GetError());
    return 1;
  }
  SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(gpu.device);
  draw(gpu, cmd, pipe, tex, w, h, g);
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
  SDL_WaitForGPUFences(gpu.device, true, &fence, 1);
  SDL_ReleaseGPUFence(gpu.device, fence);
  const auto* px = static_cast<const uint8_t*>(SDL_MapGPUTransferBuffer(gpu.device, down, false));
  write_png(out, w, h, {px, size_t(w) * h * 4});
  SDL_UnmapGPUTransferBuffer(gpu.device, down);
  SDL_Log("wrote %s after %d steps: player at (%.3f, %.3f), heading %.4f", out.string().c_str(), frames,
          g.player.x, g.player.y, g.player.heading);
  SDL_ReleaseGPUTransferBuffer(gpu.device, down);
  SDL_ReleaseGPUGraphicsPipeline(gpu.device, pipe);
  SDL_ReleaseGPUTexture(gpu.device, tex);
  return 0;
}

int16_t clamp16(int v) { return int16_t(std::clamp(v, -32768, 32767)); }

int run_window(Gpu& gpu, Game& g) {
  SDL_Window* window = SDL_CreateWindow("geow2", 1280, 720, SDL_WINDOW_RESIZABLE);
  if (!window || !SDL_ClaimWindowForGPUDevice(gpu.device, window)) {
    SDL_Log("cannot open the window: %s", SDL_GetError());
    return 1;
  }
  SDL_GPUGraphicsPipeline* pipe = make_pipeline(gpu, SDL_GetGPUSwapchainTextureFormat(gpu.device, window));
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
    // Left stick in the XInput convention (SDL's Y axis points down).
    int sx = 0, sy = 0;
    if (pad) {
      sx = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX);
      sy = -int(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY));
    }
    const bool* keys = SDL_GetKeyboardState(nullptr);
    if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT]) sx = -32768;
    if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) sx = 32767;
    if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP]) sy = 32767;
    if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]) sy = -32768;

    // Fixed 1/60 s steps from real time; the picture shows the latest step.
    const uint64_t now = SDL_GetTicksNS();
    pending += double(now - last) / 1e9;
    last = now;
    for (int n = 0; pending >= kStep && n < 8; ++n) {
      step(g, clamp16(sx), clamp16(sy));
      pending -= kStep;
    }
    if (pending > kStep) pending = 0.0;

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(gpu.device);
    SDL_GPUTexture* swap = nullptr;
    uint32_t w = 0, h = 0;
    if (SDL_WaitAndAcquireGPUSwapchainTexture(cmd, window, &swap, &w, &h) && swap) draw(gpu, cmd, pipe, swap, w, h, g);
    SDL_SubmitGPUCommandBuffer(cmd);
  }
  SDL_ReleaseGPUGraphicsPipeline(gpu.device, pipe);
  SDL_ReleaseWindowFromGPUDevice(gpu.device, window);
  SDL_DestroyWindow(window);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  fs::path cache = fs::path(SDL_GetBasePath() ? SDL_GetBasePath() : "") / "cache";
  int frames = -1;
  int16_t sx = 0, sy = 0;
  fs::path screenshot;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--data" && i + 1 < argc) cache = argv[++i];
    else if (a == "--frames" && i + 1 < argc) frames = std::atoi(argv[++i]);
    else if (a == "--screenshot" && i + 1 < argc) screenshot = argv[++i];
    else if (a == "--stick" && i + 2 < argc) {
      sx = clamp16(std::atoi(argv[++i]));
      sy = clamp16(std::atoi(argv[++i]));
    } else {
      std::fprintf(stderr, "usage: geow2 [--data <cache>] [--frames <n> --screenshot <png> [--stick <x> <y>]]\n");
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
  Gpu gpu;
  gpu.device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, false, nullptr);
  if (!gpu.device) {
    SDL_Log("no GPU device: %s", SDL_GetError());
    return 1;
  }
  const fs::path shaders = fs::path(SDL_GetBasePath() ? SDL_GetBasePath() : "") / "shaders";
  gpu.vs = load_shader(gpu.device, shaders / "line.vert.spv", SDL_GPU_SHADERSTAGE_VERTEX);
  gpu.fs = load_shader(gpu.device, shaders / "line.frag.spv", SDL_GPU_SHADERSTAGE_FRAGMENT);
  if (!gpu.vs || !gpu.fs) return 1;
  const int rc = headless ? run_headless(gpu, game, frames, sx, sy, screenshot) : run_window(gpu, game);
  if (gpu.vertices) SDL_ReleaseGPUBuffer(gpu.device, gpu.vertices);
  if (gpu.upload) SDL_ReleaseGPUTransferBuffer(gpu.device, gpu.upload);
  SDL_ReleaseGPUShader(gpu.device, gpu.vs);
  SDL_ReleaseGPUShader(gpu.device, gpu.fs);
  SDL_DestroyGPUDevice(gpu.device);
  SDL_Quit();
  return rc;
}

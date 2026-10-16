#include "app/renderer.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>

#include "data/tga.h"
#include "extract/bytes.h"
#include "game/bloom_weights.h"
#include "game/fused.h"

namespace fs = std::filesystem;

namespace geow2 {
namespace {

constexpr SDL_GPUTextureFormat kLayerFormat = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
constexpr float kAspect = 1.77777779f;  // dat_82002DB0

// The back buffer of the original is 1280 x 720, or 1920 x 1080 on a
// high-definition output (sub_8205D538); in that mode the bloom chain
// starts with one more halving step (sub_82064640, flag dat_821A1D68).
constexpr uint32_t kStandardHeight = 720;

// The light map's size (dat_821825FC, dat_82182600).
constexpr uint32_t kLightW = 150, kLightH = 100;

// Out of GPU memory for something every frame draws from: there is nothing
// sensible to draw with, so the game stops with a message.
[[noreturn]] void out_of_gpu_memory(const char* what) {
  const std::string msg = std::string("Out of graphics memory (") + what + "): " + SDL_GetError();
  SDL_Log("%s", msg.c_str());
  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Geometry Wars: Retro Evolved 2", msg.c_str(), nullptr);
  std::abort();
}

SDL_GPUShader* load_shader(SDL_GPUDevice* dev, const fs::path& path, SDL_GPUShaderStage stage, uint32_t samplers,
                           uint32_t uniforms) {
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
  info.num_samplers = samplers;
  info.num_uniform_buffers = uniforms;
  SDL_GPUShader* s = SDL_CreateGPUShader(dev, &info);
  if (!s) SDL_Log("cannot create shader %s: %s", path.string().c_str(), SDL_GetError());
  return s;
}

struct Attr {
  SDL_GPUVertexElementFormat format;
  uint32_t offset;
};

SDL_GPUGraphicsPipeline* make_pipeline(SDL_GPUDevice* dev, SDL_GPUShader* vs, SDL_GPUShader* fs,
                                       SDL_GPUTextureFormat format, bool additive, uint32_t pitch,
                                       std::initializer_list<Attr> attrs, bool per_instance = false,
                                       bool min_blend = false) {
  SDL_GPUColorTargetDescription target{};
  target.format = format;
  if (min_blend) {
    // Blend 0x1000741 (sub_82067A08): colour ONE, MIN, INV_SRC_ALPHA (the
    // factors do not apply to MIN); alpha ZERO + ONE (kept).
    target.blend_state.enable_blend = true;
    target.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    target.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    target.blend_state.color_blend_op = SDL_GPU_BLENDOP_MIN;
    target.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
    target.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    target.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
  } else if (additive) {
    // Blend 0x1010101 (sub_820679A8): ONE + ONE for colour and alpha.
    target.blend_state.enable_blend = true;
    target.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    target.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    target.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    target.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    target.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    target.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
  }
  SDL_GPUVertexBufferDescription vb{};
  vb.slot = 0;
  vb.pitch = pitch;
  vb.input_rate = per_instance ? SDL_GPU_VERTEXINPUTRATE_INSTANCE : SDL_GPU_VERTEXINPUTRATE_VERTEX;
  std::vector<SDL_GPUVertexAttribute> va;
  for (const Attr& a : attrs) va.push_back({uint32_t(va.size()), 0, a.format, a.offset});
  SDL_GPUGraphicsPipelineCreateInfo info{};
  info.vertex_shader = vs;
  info.fragment_shader = fs;
  if (pitch) {
    info.vertex_input_state.vertex_buffer_descriptions = &vb;
    info.vertex_input_state.num_vertex_buffers = 1;
    info.vertex_input_state.vertex_attributes = va.data();
    info.vertex_input_state.num_vertex_attributes = uint32_t(va.size());
  }
  info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
  info.target_info.color_target_descriptions = &target;
  info.target_info.num_color_targets = 1;
  return SDL_CreateGPUGraphicsPipeline(dev, &info);
}

// sub_82064640 halves with an arithmetic shift and keeps at least 1.
uint32_t half(uint32_t v) { return v >> 1 ? v >> 1 : 1; }

struct Rect {
  float x, y, w, h;
};

Rect full(uint32_t w, uint32_t h) { return {0.0f, 0.0f, float(w), float(h)}; }

}  // namespace

bool Renderer::init(SDL_GPUDevice* device, const fs::path& dir) {
  device_ = device;
  auto shader = [&](const char* name, SDL_GPUShaderStage stage, uint32_t samplers, uint32_t uniforms) {
    SDL_GPUShader* s = load_shader(device, dir / name, stage, samplers, uniforms);
    shaders_.push_back(s);
    return s;
  };
  constexpr auto V = SDL_GPU_SHADERSTAGE_VERTEX;
  constexpr auto F = SDL_GPU_SHADERSTAGE_FRAGMENT;
  SDL_GPUShader* line_vs = shader("line.vert.spv", V, 0, 1);
  SDL_GPUShader* ui_vs = shader("ui_line.vert.spv", V, 0, 1);
  SDL_GPUShader* ui_fill_fs = shader("ui_fill.frag.spv", F, 0, 0);
  SDL_GPUShader* ui_sprite_vs = shader("ui_sprite.vert.spv", V, 0, 1);
  SDL_GPUShader* ui_sprite_flat_vs = shader("ui_sprite_flat.vert.spv", V, 0, 0);
  SDL_GPUShader* ui_sprite_fs = shader("ui_sprite.frag.spv", F, 1, 0);
  SDL_GPUShader* body_fs = shader("line_body.frag.spv", F, 0, 0);
  SDL_GPUShader* cap_fs = shader("line_cap.frag.spv", F, 0, 0);
  SDL_GPUShader* grid_vs = shader("grid.vert.spv", V, 0, 1);
  SDL_GPUShader* grid_fs = shader("grid.frag.spv", F, 1, 0);
  SDL_GPUShader* warp_vs = shader("light_warp.vert.spv", V, 0, 0);
  SDL_GPUShader* warp_fs = shader("light_warp.frag.spv", F, 1, 0);
  SDL_GPUShader* sprite_vs = shader("light_sprite.vert.spv", V, 0, 0);
  SDL_GPUShader* sprite_fs = shader("light_sprite.frag.spv", F, 1, 0);
  SDL_GPUShader* star_vs = shader("star.vert.spv", V, 0, 1);
  SDL_GPUShader* star_fs = shader("star.frag.spv", F, 1, 0);
  SDL_GPUShader* wall_vs = shader("wall.vert.spv", V, 0, 1);
  SDL_GPUShader* wall_fs = shader("wall.frag.spv", F, 1, 0);
  SDL_GPUShader* particle_fs = shader("particle.frag.spv", F, 0, 0);
  full_vs_ = shader("fullscreen.vert.spv", V, 0, 0);
  SDL_GPUShader* downsample_fs = shader("downsample.frag.spv", F, 1, 1);
  SDL_GPUShader* blur_fs = shader("blur.frag.spv", F, 1, 1);
  SDL_GPUShader* combine_fs = shader("combine.frag.spv", F, 5, 1);
  SDL_GPUShader* light_combine_fs = shader("light_combine.frag.spv", F, 6, 1);
  SDL_GPUShader* feedback_fs = shader("light_feedback.frag.spv", F, 2, 1);
  composite_fs_ = shader("composite.frag.spv", F, 4, 1);
  for (SDL_GPUShader* s : shaders_)
    if (!s) return false;

  constexpr auto F2 = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
  constexpr auto F3 = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
  constexpr auto F4 = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
  body_ = make_pipeline(device, line_vs, body_fs, kLayerFormat, true, sizeof(LineVertex), {{F4, 0}, {F4, 16}});
  caps_ = make_pipeline(device, line_vs, cap_fs, kLayerFormat, true, sizeof(LineVertex), {{F4, 0}, {F4, 16}});
  // The UI's line presets 0x821B45B0 / 0x821B45BC: the same pixel shaders.
  ui_body_ = make_pipeline(device, ui_vs, body_fs, kLayerFormat, true, sizeof(LineVertex), {{F4, 0}, {F4, 16}});
  ui_caps_ = make_pipeline(device, ui_vs, cap_fs, kLayerFormat, true, sizeof(LineVertex), {{F4, 0}, {F4, 16}});
  // The panel fill (preset 0x821B467C) and the sprites (0x821B46D0 bent,
  // 0x821B46DC flat; ONE + ONE).
  ui_fill_ = make_pipeline(device, ui_vs, ui_fill_fs, kLayerFormat, false, sizeof(LineVertex), {{F4, 0}, {F4, 16}},
                           false, true);
  ui_sprite_ = make_pipeline(device, ui_sprite_vs, ui_sprite_fs, kLayerFormat, true, sizeof(LineVertex),
                             {{F4, 0}, {F4, 16}});
  ui_sprite_flat_ = make_pipeline(device, ui_sprite_flat_vs, ui_sprite_fs, kLayerFormat, true, sizeof(LineVertex),
                                  {{F4, 0}, {F4, 16}});
  grid_ = make_pipeline(device, grid_vs, grid_fs, kLayerFormat, true, sizeof(GridVertex), {{F3, 0}, {F2, 20}});
  warp_ = make_pipeline(device, warp_vs, warp_fs, kLayerFormat, false, sizeof(GridVertex), {{F2, 0}, {F2, 12}});
  sprites_ = make_pipeline(device, sprite_vs, sprite_fs, kLayerFormat, true, sizeof(LightVertex),
                           {{F2, 0}, {F2, 8}, {F3, 16}});
  stars_ = make_pipeline(device, star_vs, star_fs, kLayerFormat, true, sizeof(StarVertex), {{F3, 0}, {F3, 12}},
                         true);
  particles_ =
      make_pipeline(device, line_vs, particle_fs, kLayerFormat, true, sizeof(LineVertex), {{F4, 0}, {F4, 16}});
  walls_ = make_pipeline(device, wall_vs, wall_fs, kLayerFormat, true, sizeof(GridVertex),
                         {{F3, 0}, {F2, 12}, {SDL_GPU_VERTEXELEMENTFORMAT_FLOAT, 24}});
  downsample_ = make_pipeline(device, full_vs_, downsample_fs, kLayerFormat, false, 0, {});
  blur_ = make_pipeline(device, full_vs_, blur_fs, kLayerFormat, false, 0, {});
  combine_ = make_pipeline(device, full_vs_, combine_fs, kLayerFormat, false, 0, {});
  light_combine_ = make_pipeline(device, full_vs_, light_combine_fs, kLayerFormat, false, 0, {});
  feedback_ = make_pipeline(device, full_vs_, feedback_fs, kLayerFormat, false, 0, {});

  SDL_GPUSamplerCreateInfo si{};
  si.min_filter = SDL_GPU_FILTER_NEAREST;
  si.mag_filter = SDL_GPU_FILTER_NEAREST;
  si.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
  si.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  si.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  sampler_ = SDL_CreateGPUSampler(device, &si);
  si.min_filter = SDL_GPU_FILTER_LINEAR;
  si.mag_filter = SDL_GPU_FILTER_LINEAR;
  sprite_sampler_ = SDL_CreateGPUSampler(device, &si);

  SDL_GPUTextureCreateInfo tci{};
  tci.type = SDL_GPU_TEXTURETYPE_2D;
  tci.format = SDL_GPU_TEXTUREFORMAT_R8_UNORM;
  tci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
  tci.width = kLightSpriteSize;
  tci.height = kLightSpriteSize;
  tci.layer_count_or_depth = 1;
  tci.num_levels = 1;
  sprite_ = SDL_CreateGPUTexture(device, &tci);
  tci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
  tci.width = 3;
  tci.height = 3;
  star_sprite_ = SDL_CreateGPUTexture(device, &tci);

  light_ = make_target(kLightW, kLightH);
  light_warped_ = make_target(kLightW, kLightH);
  light_new_ = make_target(kLightW, kLightH);
  light_combined_ = make_target(kLightW, kLightH);
  make_chain(light_chain_, kLightW, kLightH, false);

  if (!body_ || !caps_ || !ui_body_ || !ui_caps_ || !ui_fill_ || !ui_sprite_ || !ui_sprite_flat_ || !grid_ || !warp_ || !sprites_ || !stars_ || !walls_ || !particles_ || !star_sprite_ || !downsample_ || !blur_ || !combine_ || !light_combine_ ||
      !feedback_ || !sampler_ || !sprite_sampler_ || !sprite_ || !light_.tex) {
    SDL_Log("cannot create the render resources: %s", SDL_GetError());
    return false;
  }

  // Static data: the sprite texture and the warp mesh's quads as triangles.
  SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
  const std::vector<uint8_t> sprite = light_sprite_texture();
  SDL_GPUTransferBufferCreateInfo ti{SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, uint32_t(sprite.size()), 0};
  SDL_GPUTransferBuffer* t = SDL_CreateGPUTransferBuffer(device, &ti);
  void* tp = t ? SDL_MapGPUTransferBuffer(device, t, false) : nullptr;
  if (!cmd || !tp) out_of_gpu_memory("the render resources");
  std::memcpy(tp, sprite.data(), sprite.size());
  SDL_UnmapGPUTransferBuffer(device, t);
  SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
  SDL_GPUTextureTransferInfo src{t, 0, kLightSpriteSize, kLightSpriteSize};
  SDL_GPUTextureRegion dst{};
  dst.texture = sprite_;
  dst.w = kLightSpriteSize;
  dst.h = kLightSpriteSize;
  dst.d = 1;
  SDL_UploadToGPUTexture(copy, &src, &dst, false);
  // The star sprite (sub_82075318): 3 x 3, all zero but the middle texel,
  // which is 0xFFFFFFFF.
  uint8_t star[3 * 3 * 4] = {};
  for (int c = 0; c < 4; ++c) star[(1 * 3 + 1) * 4 + c] = 0xFF;
  SDL_GPUTransferBufferCreateInfo sti{SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, sizeof(star), 0};
  SDL_GPUTransferBuffer* st = SDL_CreateGPUTransferBuffer(device, &sti);
  void* sp = st ? SDL_MapGPUTransferBuffer(device, st, false) : nullptr;
  if (!sp) out_of_gpu_memory("the render resources");
  std::memcpy(sp, star, sizeof(star));
  SDL_UnmapGPUTransferBuffer(device, st);
  SDL_GPUTextureTransferInfo ssrc{st, 0, 3, 3};
  SDL_GPUTextureRegion sdst{};
  sdst.texture = star_sprite_;
  sdst.w = 3;
  sdst.h = 3;
  sdst.d = 1;
  SDL_UploadToGPUTexture(copy, &ssrc, &sdst, false);
  SDL_EndGPUCopyPass(copy);
  std::vector<uint16_t> tri;
  const std::vector<uint16_t> quads = grid_warp_indices();
  for (size_t q = 0; q + 3 < quads.size(); q += 4)
    for (int i : {0, 1, 2, 0, 2, 3}) tri.push_back(quads[q + size_t(i)]);
  warp_index_count_ = uint32_t(tri.size());
  upload(cmd, warp_index_, SDL_GPU_BUFFERUSAGE_INDEX, tri.data(), uint32_t(tri.size() * sizeof(uint16_t)));
  SDL_SubmitGPUCommandBuffer(cmd);
  SDL_ReleaseGPUTransferBuffer(device, t);
  SDL_ReleaseGPUTransferBuffer(device, st);
  return true;
}

void Renderer::load_ui_textures(const fs::path& dir) {
  static const char* kFiles[kTexAchievement0] = {"logo0.tga", "star.tga", "controller.tga", "retroatvi.tga",
                                                 "retrobizarre.tga"};
  for (int i = 0; i < kTexAchievement0; ++i) {
    std::optional<Image> img;
    try {
      img = decode_tga(read_file(dir / kFiles[i]));
    } catch (const std::exception&) {
    }
    if (!img) {
      SDL_Log("no UI texture %s", kFiles[i]);
      continue;
    }
    set_ui_texture(i, *img);
  }
}

void Renderer::set_ui_texture(int index, const Image& img) {
  if (index < 0 || index >= kUiTextureCount || img.width == 0 || img.height == 0) return;
  SDL_GPUTexture*& slot = ui_textures_[size_t(index)];
  if (slot) {
    SDL_ReleaseGPUTexture(device_, slot);
    slot = nullptr;
  }
  SDL_GPUTextureCreateInfo tci{};
  tci.type = SDL_GPU_TEXTURETYPE_2D;
  tci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
  tci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
  tci.width = img.width;
  tci.height = img.height;
  tci.layer_count_or_depth = 1;
  tci.num_levels = 1;
  SDL_GPUTexture* tex = SDL_CreateGPUTexture(device_, &tci);
  if (!tex) return;
  const uint32_t bytes = img.width * img.height * 4;
  SDL_GPUTransferBufferCreateInfo ti{SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, bytes, 0};
  SDL_GPUTransferBuffer* up = SDL_CreateGPUTransferBuffer(device_, &ti);
  void* p = up ? SDL_MapGPUTransferBuffer(device_, up, false) : nullptr;
  if (!p) {
    SDL_Log("cannot upload a %ux%u texture: %s", img.width, img.height, SDL_GetError());
    if (up) SDL_ReleaseGPUTransferBuffer(device_, up);
    SDL_ReleaseGPUTexture(device_, tex);
    return;
  }
  std::memcpy(p, img.rgba.data(), bytes);
  SDL_UnmapGPUTransferBuffer(device_, up);
  SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
  SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
  SDL_GPUTextureTransferInfo src{up, 0, img.width, img.height};
  SDL_GPUTextureRegion dst{};
  dst.texture = tex;
  dst.w = img.width;
  dst.h = img.height;
  dst.d = 1;
  SDL_UploadToGPUTexture(copy, &src, &dst, false);
  SDL_EndGPUCopyPass(copy);
  SDL_SubmitGPUCommandBuffer(cmd);
  SDL_ReleaseGPUTransferBuffer(device_, up);
  slot = tex;
}

void Renderer::release() {
  if (!device_) return;
  resize(0, 0);
  for (Target* t : {&light_, &light_warped_, &light_new_, &light_combined_}) free_target(*t);
  free_chain(light_chain_);
  for (SDL_GPUGraphicsPipeline* p : {body_, caps_, ui_body_, ui_caps_, ui_fill_, ui_sprite_, ui_sprite_flat_, grid_, warp_, sprites_, stars_, walls_, particles_, downsample_, blur_, combine_,
                                     light_combine_, feedback_, composite_})
    if (p) SDL_ReleaseGPUGraphicsPipeline(device_, p);
  for (SDL_GPUShader* s : shaders_)
    if (s) SDL_ReleaseGPUShader(device_, s);
  for (SDL_GPUSampler* s : {sampler_, sprite_sampler_})
    if (s) SDL_ReleaseGPUSampler(device_, s);
  for (SDL_GPUTexture* t : {sprite_, star_sprite_})
    if (t) SDL_ReleaseGPUTexture(device_, t);
  for (SDL_GPUTexture*& t : ui_textures_)
    if (t) {
      SDL_ReleaseGPUTexture(device_, t);
      t = nullptr;
    }
  for (Buffer* b : {&ui_fill_vertices_, &ui_sprite_vertices_, &icon_lines_, &lines_, &ui_lines_, &linger_lines_, &particle_vertices_, &grid_vertices_, &wall_vertices_, &warp_vertices_, &sprite_vertices_, &star_vertices_,
                    &quad_index_, &warp_index_}) {
    if (b->buf) SDL_ReleaseGPUBuffer(device_, b->buf);
    if (b->staging) SDL_ReleaseGPUTransferBuffer(device_, b->staging);
  }
  device_ = nullptr;
}

Renderer::Target Renderer::make_target(uint32_t w, uint32_t h) {
  SDL_GPUTextureCreateInfo tci{};
  tci.type = SDL_GPU_TEXTURETYPE_2D;
  tci.format = kLayerFormat;
  tci.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
  tci.width = w;
  tci.height = h;
  tci.layer_count_or_depth = 1;
  tci.num_levels = 1;
  return {SDL_CreateGPUTexture(device_, &tci), w, h};
}

void Renderer::free_target(Target& t) {
  if (t.tex) SDL_ReleaseGPUTexture(device_, t.tex);
  t = {};
}

void Renderer::make_chain(Chain& c, uint32_t w, uint32_t h, bool extra) {
  // Sizes as sub_82064640 derives them: (extra,) first, then each level.
  if (extra) {
    w = half(w);
    h = half(h);
    c.extra = make_target(w, h);
  }
  w = half(w);
  h = half(h);
  c.first = make_target(w, h);
  for (size_t i = 0; i < c.level.size(); ++i) {
    w = half(w);
    h = half(h);
    c.level[i] = make_target(w, h);
    c.temp[i] = make_target(w, h);
  }
}

void Renderer::free_chain(Chain& c) {
  free_target(c.extra);
  free_target(c.first);
  for (Target& t : c.level) free_target(t);
  for (Target& t : c.temp) free_target(t);
}

void Renderer::resize(uint32_t w, uint32_t h) {
  if (w == width_ && h == height_) return;
  for (Target* t : {&layer_a_, &layer_b_}) free_target(*t);
  for (Chain* c : {&chain_a_, &chain_b_}) free_chain(*c);
  width_ = w;
  height_ = h;
  if (w == 0 || h == 0) return;
  // Layer targets at the picture size (sub_82065270).
  layer_a_ = make_target(w, h);
  layer_b_ = make_target(w, h);
  for (Chain* c : {&chain_a_, &chain_b_}) make_chain(*c, w, h, h > kStandardHeight);
}

void Renderer::upload(SDL_GPUCommandBuffer* cmd, Buffer& b, SDL_GPUBufferUsageFlags usage, const void* data,
                      uint32_t size) {
  if (size == 0) return;
  if (size > b.capacity) {
    // Doubling, from 64 KB, so a busy stretch reallocates only a few times.
    uint32_t cap = b.capacity ? b.capacity : 65536;
    while (cap < size) cap *= 2;
    if (b.buf) SDL_ReleaseGPUBuffer(device_, b.buf);
    if (b.staging) SDL_ReleaseGPUTransferBuffer(device_, b.staging);
    SDL_GPUBufferCreateInfo bi{usage, cap, 0};
    b.buf = SDL_CreateGPUBuffer(device_, &bi);
    SDL_GPUTransferBufferCreateInfo ti{SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, cap, 0};
    b.staging = SDL_CreateGPUTransferBuffer(device_, &ti);
    b.capacity = cap;
    if (!b.buf || !b.staging) out_of_gpu_memory("a vertex or index buffer");
  }
  void* p = SDL_MapGPUTransferBuffer(device_, b.staging, true);
  if (!p) out_of_gpu_memory("a vertex or index buffer");
  std::memcpy(p, data, size);
  SDL_UnmapGPUTransferBuffer(device_, b.staging);
  SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
  SDL_GPUTransferBufferLocation src{b.staging, 0};
  SDL_GPUBufferRegion dst{b.buf, 0, size};
  SDL_UploadToGPUBuffer(copy, &src, &dst, true);
  SDL_EndGPUCopyPass(copy);
}

void Renderer::quad_indices(SDL_GPUCommandBuffer* cmd, uint32_t quads) {
  // Quad lists (primitive 0xD) as triangle pairs (0, 1, 2), (0, 2, 3).
  if (quads <= quad_index_quads_) return;
  uint32_t n = quad_index_quads_ ? quad_index_quads_ : 65536;
  while (n < quads) n *= 2;
  std::vector<uint32_t> idx;
  idx.reserve(size_t(n) * 6);
  for (uint32_t q = 0; q < n; ++q)
    for (uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) idx.push_back(q * 4 + i);
  upload(cmd, quad_index_, SDL_GPU_BUFFERUSAGE_INDEX, idx.data(), uint32_t(idx.size() * sizeof(uint32_t)));
  quad_index_quads_ = n;
}

void Renderer::pass(SDL_GPUCommandBuffer* cmd, const Target& dst, SDL_GPUGraphicsPipeline* pipe,
                    std::initializer_list<SDL_GPUTexture*> textures, const void* uniforms, uint32_t uniform_size) {
  SDL_GPUColorTargetInfo ct{};
  ct.texture = dst.tex;
  ct.load_op = SDL_GPU_LOADOP_DONT_CARE;
  ct.store_op = SDL_GPU_STOREOP_STORE;
  SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(cmd, &ct, 1, nullptr);
  SDL_BindGPUGraphicsPipeline(rp, pipe);
  SDL_GPUTextureSamplerBinding bindings[6]{};
  uint32_t n = 0;
  for (SDL_GPUTexture* t : textures) bindings[n++] = {t, sampler_};
  SDL_BindGPUFragmentSamplers(rp, 0, bindings, n);
  SDL_PushGPUFragmentUniformData(cmd, 0, uniforms, uniform_size);
  SDL_DrawGPUPrimitives(rp, 3, 1, 0, 0);
  SDL_EndGPURenderPass(rp);
}

void Renderer::blur_levels(SDL_GPUCommandBuffer* cmd, const Target& layer, Chain& c, float radius,
                           float precision) {
  // sub_82064930 per level: downsample (an extra step first in high
  // definition, and the first step for level 0), then the blur across and
  // down; each level starts from the previous level's result.
  const Target* src = &layer;
  for (Target* step : {&c.extra, &c.first}) {
    if (!step->tex) continue;
    const Rect r = full(step->w, step->h);
    pass(cmd, *step, downsample_, {src->tex}, &r, sizeof(r));
    src = step;
  }
  const std::array<float, 8> w = blur_weights(radius, precision);
  for (size_t i = 0; i < c.level.size(); ++i) {
    const Rect r = full(c.level[i].w, c.level[i].h);
    pass(cmd, c.level[i], downsample_, {src->tex}, &r, sizeof(r));
    struct {
      Rect rect;
      float axis[4];
      float c0[4], c1[4];
    } blur{r, {1, 0, 0, 0}, {w[0], w[1], w[2], w[3]}, {w[4], w[5], w[6], w[7]}};
    pass(cmd, c.temp[i], blur_, {c.level[i].tex}, &blur, sizeof(blur));  // preset 0x821B45F8
    blur.axis[0] = 0;
    blur.axis[1] = 1;
    pass(cmd, c.level[i], blur_, {c.temp[i].tex}, &blur, sizeof(blur));  // preset 0x821B4604
    src = &c.level[i];
  }
}

void Renderer::bloom(SDL_GPUCommandBuffer* cmd, const Target& layer, Chain& c) {
  blur_levels(cmd, layer, c, tuning.radius, tuning.precision);
  // sub_82064F88: the five levels combined at level 0's size.
  const std::array<float, 5> k = combine_weights(tuning.precision, tuning.bloom_falloff);
  struct {
    Rect rect;
    float c0[4], c1[4];
  } combine{full(c.temp[0].w, c.temp[0].h), {k[0], k[1], k[2], 0}, {k[3], k[4], 0, 0}};
  pass(cmd, c.temp[0], combine_,
       {c.level[0].tex, c.level[1].tex, c.level[2].tex, c.level[3].tex, c.level[4].tex}, &combine,
       sizeof(combine));
}

void Renderer::update_light_map(SDL_GPUCommandBuffer* cmd, const std::vector<GridVertex>& warp,
                                const std::vector<LightVertex>& sprites) {
  const uint32_t sprite_quads = uint32_t(sprites.size() / 4);
  quad_indices(cmd, sprite_quads);
  upload(cmd, warp_vertices_, SDL_GPU_BUFFERUSAGE_VERTEX, warp.data(), uint32_t(warp.size() * sizeof(GridVertex)));
  upload(cmd, sprite_vertices_, SDL_GPU_BUFFERUSAGE_VERTEX, sprites.data(),
         uint32_t(sprites.size() * sizeof(LightVertex)));
  if (!light_cleared_) {
    // The map starts empty.
    SDL_GPUColorTargetInfo ct{};
    ct.texture = light_.tex;
    ct.load_op = SDL_GPU_LOADOP_CLEAR;
    ct.store_op = SDL_GPU_STOREOP_STORE;
    SDL_EndGPURenderPass(SDL_BeginGPURenderPass(cmd, &ct, 1, nullptr));
    light_cleared_ = true;
  }

  // 1. Last step's map warped along the grid (preset 0x821B4670).
  {
    SDL_GPUColorTargetInfo ct{};
    ct.texture = light_warped_.tex;
    ct.load_op = SDL_GPU_LOADOP_CLEAR;
    ct.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(cmd, &ct, 1, nullptr);
    if (!warp.empty()) {
      SDL_BindGPUGraphicsPipeline(rp, warp_);
      const SDL_GPUBufferBinding vb{warp_vertices_.buf, 0};
      SDL_BindGPUVertexBuffers(rp, 0, &vb, 1);
      const SDL_GPUBufferBinding ib{warp_index_.buf, 0};
      SDL_BindGPUIndexBuffer(rp, &ib, SDL_GPU_INDEXELEMENTSIZE_16BIT);
      const SDL_GPUTextureSamplerBinding tex{light_.tex, sampler_};
      SDL_BindGPUFragmentSamplers(rp, 0, &tex, 1);
      SDL_DrawGPUIndexedPrimitives(rp, warp_index_count_, 1, 0, 0, 0);
    }
    SDL_EndGPURenderPass(rp);
  }
  // 2. This step's light sprites (preset 0x821B46C4), additively.
  {
    SDL_GPUColorTargetInfo ct{};
    ct.texture = light_new_.tex;
    ct.clear_color = {0, 0, 0, 0};
    ct.load_op = SDL_GPU_LOADOP_CLEAR;
    ct.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(cmd, &ct, 1, nullptr);
    if (sprite_quads) {
      SDL_BindGPUGraphicsPipeline(rp, sprites_);
      const SDL_GPUBufferBinding vb{sprite_vertices_.buf, 0};
      SDL_BindGPUVertexBuffers(rp, 0, &vb, 1);
      const SDL_GPUBufferBinding ib{quad_index_.buf, 0};
      SDL_BindGPUIndexBuffer(rp, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);
      const SDL_GPUTextureSamplerBinding tex{sprite_, sprite_sampler_};
      SDL_BindGPUFragmentSamplers(rp, 0, &tex, 1);
      SDL_DrawGPUIndexedPrimitives(rp, sprite_quads * 6, 1, 0, 0, 0);
    }
    SDL_EndGPURenderPass(rp);
  }
  // 3. Its bloom (table 0x821B3730) and 4. the combine (sub_82064C90).
  const RenderTuning& t = tuning;
  blur_levels(cmd, light_new_, light_chain_, t.light_radius, t.light_precision);
  const std::array<float, 5> k = light_combine_weights(t.light_contrast, t.light_bloom, t.light_precision, 1.0f);
  struct {
    Rect rect;
    float c0[4], c1[4];
  } combine{full(kLightW, kLightH), {t.light_contrast, k[0], k[1], 0}, {k[2], k[3], k[4], 0}};
  const Chain& c = light_chain_;
  pass(cmd, light_combined_, light_combine_,
       {light_new_.tex, c.level[0].tex, c.level[1].tex, c.level[2].tex, c.level[3].tex, c.level[4].tex}, &combine,
       sizeof(combine));
  // 5. Feedback (preset 0x821B4664): this step's map for the grid.
  const struct {
    Rect rect;
    float c0[4];
  } feedback{full(kLightW, kLightH), {t.light_feedback_mul, t.light_feedback_add, 0, 0}};
  pass(cmd, light_, feedback_, {light_combined_.tex, light_warped_.tex}, &feedback, sizeof(feedback));
}

SDL_GPUGraphicsPipeline* Renderer::composite_pipeline(SDL_GPUTextureFormat format) {
  if (composite_ && composite_format_ == format) return composite_;
  if (composite_) SDL_ReleaseGPUGraphicsPipeline(device_, composite_);
  composite_ = make_pipeline(device_, full_vs_, composite_fs_, format, false, 0, {});
  composite_format_ = format;
  return composite_;
}

void Renderer::draw(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* out, SDL_GPUTextureFormat format, uint32_t out_w,
                    uint32_t out_h, const Mat4& vp, const std::vector<StarVertex>& stars,
                    const std::vector<GridVertex>* grid, const std::vector<GridVertex>& walls,
                    const std::vector<LineVertex>& particles, const LineBatch& linger, const LineBatch& foreground,
                    const UiCanvas& canvas, const Mat4& ui_transform, const Mat4& icon_vp) {
  const LineBatch& ui = canvas.lines;
  // The 16:9 picture, letterboxed into the output.
  uint32_t pw = out_w, ph = uint32_t(float(out_w) / kAspect + 0.5f);
  if (ph > out_h) {
    ph = out_h;
    pw = uint32_t(float(out_h) * kAspect + 0.5f);
  }
  if (pw == 0 || ph == 0) return;
  resize(pw, ph);

  // Foreground quads: bodies then caps, in one buffer.
  std::vector<LineVertex> v(foreground.body);
  v.insert(v.end(), foreground.caps.begin(), foreground.caps.end());
  const uint32_t body_quads = uint32_t(foreground.body.size() / 4), cap_quads = uint32_t(foreground.caps.size() / 4);
  const uint32_t grid_quads = grid ? uint32_t(grid->size() / 4) : 0;
  const uint32_t particle_quads = uint32_t(particles.size() / 4);
  std::vector<LineVertex> uv(ui.body);
  uv.insert(uv.end(), ui.caps.begin(), ui.caps.end());
  const uint32_t ui_body = uint32_t(ui.body.size() / 4), ui_caps = uint32_t(ui.caps.size() / 4);
  std::vector<LineVertex> lv(linger.body);
  lv.insert(lv.end(), linger.caps.begin(), linger.caps.end());
  const uint32_t linger_body = uint32_t(linger.body.size() / 4), linger_caps = uint32_t(linger.caps.size() / 4);
  upload(cmd, linger_lines_, SDL_GPU_BUFFERUSAGE_VERTEX, lv.data(), uint32_t(lv.size() * sizeof(LineVertex)));
  // The panel fill: (x, y, 0, side 0, rgb * 0.1, a) (sub_82091E98; the
  // pixel shader ignores the colour).
  std::vector<LineVertex> fv;
  fv.reserve(canvas.fill.size());
  for (const UiFillVertex& f : canvas.fill) fv.push_back({f.x, f.y, 0.0f, 0.0f, f.r * 0.1f, f.g * 0.1f, f.b * 0.1f, f.a});
  const uint32_t fill_quads = uint32_t(fv.size() / 4);
  // The sprites as quads: bent ones tessellated into 1 + trunc(w / 20) by
  // 1 + trunc(h / 20) cells (sub_8205D980), flat ones one quad
  // (sub_8205DD38); colour (a * 8r, a * 8g, a * 8b).
  std::vector<LineVertex> sv;
  struct SpriteDraw {
    uint32_t first, quads;
    int texture;
    bool flat;
  };
  std::vector<SpriteDraw> sprite_draws;
  for (const bool flat : {false, true})
    for (const UiSprite& s : canvas.sprites) {
      if (s.flat != flat || s.texture < 0 || s.texture >= kUiTextureCount || !ui_textures_[size_t(s.texture)]) continue;
      const float c[4] = {s.a * s.r, s.a * s.g, s.a * s.b, 1.0f};
      const uint32_t first = uint32_t(sv.size() / 4);
      if (flat) {
        sv.push_back({s.x, s.y, 0.0f, 0.0f, c[0], c[1], c[2], c[3]});
        sv.push_back({s.x + s.w, s.y, 1.0f, 0.0f, c[0], c[1], c[2], c[3]});
        sv.push_back({s.x + s.w, s.y + s.h, 1.0f, 1.0f, c[0], c[1], c[2], c[3]});
        sv.push_back({s.x, s.y + s.h, 0.0f, 1.0f, c[0], c[1], c[2], c[3]});
      } else {
        const int cols = 1 - int(s.w * -0.05f), rows = 1 - int(s.h * -0.05f);  // dat_820028F0
        for (int j = 0; j < rows; ++j) {
          const float v0 = float(j) / float(rows), v1 = float(j + 1) / float(rows);
          const float y0 = fmadds(s.h, v0, s.y), y1 = fmadds(s.h, v1, s.y);
          for (int i = 0; i < cols; ++i) {
            const float u0 = float(i) / float(cols), u1 = float(i + 1) / float(cols);
            const float x0 = fmadds(s.w, u0, s.x), x1 = fmadds(s.w, u1, s.x);
            sv.push_back({x0, y0, u0, v0, c[0], c[1], c[2], c[3]});
            sv.push_back({x1, y0, u1, v0, c[0], c[1], c[2], c[3]});
            sv.push_back({x1, y1, u1, v1, c[0], c[1], c[2], c[3]});
            sv.push_back({x0, y1, u0, v1, c[0], c[1], c[2], c[3]});
          }
        }
      }
      sprite_draws.push_back({first, uint32_t(sv.size() / 4) - first, s.texture, flat});
    }
  std::vector<LineVertex> iv(canvas.icons.body);
  iv.insert(iv.end(), canvas.icons.caps.begin(), canvas.icons.caps.end());
  const uint32_t icon_body = uint32_t(canvas.icons.body.size() / 4), icon_caps = uint32_t(canvas.icons.caps.size() / 4);
  quad_indices(cmd, std::max({body_quads + cap_quads, grid_quads, particle_quads, ui_body + ui_caps,
                              linger_body + linger_caps, fill_quads, uint32_t(sv.size() / 4), icon_body + icon_caps}));
  upload(cmd, ui_fill_vertices_, SDL_GPU_BUFFERUSAGE_VERTEX, fv.data(), uint32_t(fv.size() * sizeof(LineVertex)));
  upload(cmd, ui_sprite_vertices_, SDL_GPU_BUFFERUSAGE_VERTEX, sv.data(), uint32_t(sv.size() * sizeof(LineVertex)));
  upload(cmd, icon_lines_, SDL_GPU_BUFFERUSAGE_VERTEX, iv.data(), uint32_t(iv.size() * sizeof(LineVertex)));
  upload(cmd, ui_lines_, SDL_GPU_BUFFERUSAGE_VERTEX, uv.data(), uint32_t(uv.size() * sizeof(LineVertex)));
  upload(cmd, particle_vertices_, SDL_GPU_BUFFERUSAGE_VERTEX, particles.data(),
         uint32_t(particles.size() * sizeof(LineVertex)));
  upload(cmd, lines_, SDL_GPU_BUFFERUSAGE_VERTEX, v.data(), uint32_t(v.size() * sizeof(LineVertex)));
  if (grid_quads)
    upload(cmd, grid_vertices_, SDL_GPU_BUFFERUSAGE_VERTEX, grid->data(), uint32_t(grid->size() * sizeof(GridVertex)));
  upload(cmd, star_vertices_, SDL_GPU_BUFFERUSAGE_VERTEX, stars.data(), uint32_t(stars.size() * sizeof(StarVertex)));
  upload(cmd, wall_vertices_, SDL_GPU_BUFFERUSAGE_VERTEX, walls.data(), uint32_t(walls.size() * sizeof(GridVertex)));

  auto begin_layer = [&](const Target& t) {
    // Cleared to Render.ClearColour (0x821B39CC, default (0, 0, 0, 0)).
    SDL_GPUColorTargetInfo ct{};
    ct.texture = t.tex;
    ct.clear_color = {0, 0, 0, 0};
    ct.load_op = SDL_GPU_LOADOP_CLEAR;
    ct.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(cmd, &ct, 1, nullptr);
    SDL_PushGPUVertexUniformData(cmd, 0, vp.m, sizeof(vp.m));
    const SDL_GPUBufferBinding ib{quad_index_.buf, 0};
    SDL_BindGPUIndexBuffer(rp, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    return rp;
  };

  // Layer A (sub_820663A8): the stars (sub_820756F8, preset 0x821B4658),
  // then the grid (sub_8205CAF0, preset 0x821B45C8), lit by the light map.
  SDL_GPURenderPass* rp = begin_layer(layer_a_);
  if (!stars.empty()) {
    // 3 pixels of the original's back buffer (720 or 1080 lines).
    const float px = 3.0f * float(ph) / float(ph > kStandardHeight ? 1080 : 720);
    struct {
      float m[16];
      float size[4];
    } u{};
    std::memcpy(u.m, vp.m, sizeof(u.m));
    u.size[0] = 2.0f * px / float(pw);
    u.size[1] = 2.0f * px / float(ph);
    SDL_PushGPUVertexUniformData(cmd, 0, &u, sizeof(u));
    SDL_BindGPUGraphicsPipeline(rp, stars_);
    const SDL_GPUBufferBinding vb{star_vertices_.buf, 0};
    SDL_BindGPUVertexBuffers(rp, 0, &vb, 1);
    const SDL_GPUTextureSamplerBinding tex{star_sprite_, sprite_sampler_};
    SDL_BindGPUFragmentSamplers(rp, 0, &tex, 1);
    SDL_DrawGPUPrimitives(rp, 6, uint32_t(stars.size()), 0, 0);
    SDL_PushGPUVertexUniformData(cmd, 0, vp.m, sizeof(vp.m));
  }
  if (grid_quads) {
    SDL_BindGPUGraphicsPipeline(rp, grid_);
    const SDL_GPUBufferBinding vb{grid_vertices_.buf, 0};
    SDL_BindGPUVertexBuffers(rp, 0, &vb, 1);
    const SDL_GPUTextureSamplerBinding tex{light_.tex, sampler_};
    SDL_BindGPUFragmentSamplers(rp, 0, &tex, 1);
    SDL_DrawGPUIndexedPrimitives(rp, grid_quads * 6, 1, 0, 0, 0);
  }
  // The walls (sub_8205CF20, preset 0x821B4688), lit by the light map's
  // level-1 bloom (0x821B3794).
  if (!walls.empty()) {
    SDL_BindGPUGraphicsPipeline(rp, walls_);
    const SDL_GPUBufferBinding vb{wall_vertices_.buf, 0};
    SDL_BindGPUVertexBuffers(rp, 0, &vb, 1);
    const SDL_GPUTextureSamplerBinding tex{light_chain_.level[1].tex, sampler_};
    SDL_BindGPUFragmentSamplers(rp, 0, &tex, 1);
    SDL_DrawGPUIndexedPrimitives(rp, uint32_t(walls.size() / 4) * 6, 1, 0, 0, 0);
  }
  // Particle streaks (preset 0x821B4610).
  if (particle_quads) {
    SDL_BindGPUGraphicsPipeline(rp, particles_);
    const SDL_GPUBufferBinding vb{particle_vertices_.buf, 0};
    SDL_BindGPUVertexBuffers(rp, 0, &vb, 1);
    SDL_DrawGPUIndexedPrimitives(rp, particle_quads * 6, 1, 0, 0, 0);
  }
  // The lingering numbers (Render.Layer.Linger): UI lines in world space
  // with the world camera and the line presets 0x821B4694 / 0x821B46B8.
  if (linger_body + linger_caps) {
    const SDL_GPUBufferBinding vb{linger_lines_.buf, 0};
    SDL_BindGPUVertexBuffers(rp, 0, &vb, 1);
    SDL_BindGPUGraphicsPipeline(rp, body_);
    if (linger_body) SDL_DrawGPUIndexedPrimitives(rp, linger_body * 6, 1, 0, 0, 0);
    SDL_BindGPUGraphicsPipeline(rp, caps_);
    if (linger_caps) SDL_DrawGPUIndexedPrimitives(rp, linger_caps * 6, 1, 0, int32_t(linger_body * 4), 0);
  }
  // The UI block's end in layer A: the panel fill caps what is behind it.
  if (fill_quads) {
    SDL_PushGPUVertexUniformData(cmd, 0, ui_transform.m, sizeof(ui_transform.m));
    const SDL_GPUBufferBinding vb{ui_fill_vertices_.buf, 0};
    SDL_BindGPUVertexBuffers(rp, 0, &vb, 1);
    SDL_BindGPUGraphicsPipeline(rp, ui_fill_);
    SDL_DrawGPUIndexedPrimitives(rp, fill_quads * 6, 1, 0, 0, 0);
  }
  SDL_EndGPURenderPass(rp);

  // Layer B (sub_820669F0): line bodies (preset 0x821B4694), then caps
  // (0x821B46B8).
  rp = begin_layer(layer_b_);
  if (body_quads + cap_quads) {
    const SDL_GPUBufferBinding vb{lines_.buf, 0};
    SDL_BindGPUVertexBuffers(rp, 0, &vb, 1);
    SDL_BindGPUGraphicsPipeline(rp, body_);
    if (body_quads) SDL_DrawGPUIndexedPrimitives(rp, body_quads * 6, 1, 0, 0, 0);
    SDL_BindGPUGraphicsPipeline(rp, caps_);
    if (cap_quads) SDL_DrawGPUIndexedPrimitives(rp, cap_quads * 6, 1, 0, int32_t(body_quads * 4), 0);
  }
  // The UI (Render.Layer.Ui in layer B): the panel fill again (its
  // additive second draw adds nothing to the colour of a black fill), then
  // bodies 0x821A4108 and caps 0x821A4948, with the UI transform,
  // additive; then the sprites, bent then flat; then the model icons with
  // their own camera.
  SDL_PushGPUVertexUniformData(cmd, 0, ui_transform.m, sizeof(ui_transform.m));
  if (fill_quads) {
    const SDL_GPUBufferBinding vb{ui_fill_vertices_.buf, 0};
    SDL_BindGPUVertexBuffers(rp, 0, &vb, 1);
    SDL_BindGPUGraphicsPipeline(rp, ui_fill_);
    SDL_DrawGPUIndexedPrimitives(rp, fill_quads * 6, 1, 0, 0, 0);
  }
  if (ui_body + ui_caps) {
    const SDL_GPUBufferBinding vb{ui_lines_.buf, 0};
    SDL_BindGPUVertexBuffers(rp, 0, &vb, 1);
    SDL_BindGPUGraphicsPipeline(rp, ui_body_);
    if (ui_body) SDL_DrawGPUIndexedPrimitives(rp, ui_body * 6, 1, 0, 0, 0);
    SDL_BindGPUGraphicsPipeline(rp, ui_caps_);
    if (ui_caps) SDL_DrawGPUIndexedPrimitives(rp, ui_caps * 6, 1, 0, int32_t(ui_body * 4), 0);
  }
  if (!sprite_draws.empty()) {
    const SDL_GPUBufferBinding vb{ui_sprite_vertices_.buf, 0};
    SDL_BindGPUVertexBuffers(rp, 0, &vb, 1);
    for (const SpriteDraw& d : sprite_draws) {
      SDL_BindGPUGraphicsPipeline(rp, d.flat ? ui_sprite_flat_ : ui_sprite_);
      const SDL_GPUTextureSamplerBinding tex{ui_textures_[size_t(d.texture)], sampler_};
      SDL_BindGPUFragmentSamplers(rp, 0, &tex, 1);
      SDL_DrawGPUIndexedPrimitives(rp, d.quads * 6, 1, d.first * 6, 0, 0);
    }
  }
  if (icon_body + icon_caps) {
    SDL_PushGPUVertexUniformData(cmd, 0, icon_vp.m, sizeof(icon_vp.m));
    const SDL_GPUBufferBinding vb{icon_lines_.buf, 0};
    SDL_BindGPUVertexBuffers(rp, 0, &vb, 1);
    SDL_BindGPUGraphicsPipeline(rp, body_);
    if (icon_body) SDL_DrawGPUIndexedPrimitives(rp, icon_body * 6, 1, 0, 0, 0);
    SDL_BindGPUGraphicsPipeline(rp, caps_);
    if (icon_caps) SDL_DrawGPUIndexedPrimitives(rp, icon_caps * 6, 1, 0, int32_t(icon_body * 4), 0);
  }
  SDL_EndGPURenderPass(rp);

  bloom(cmd, layer_a_, chain_a_);
  bloom(cmd, layer_b_, chain_b_);

  // Composite (preset 0x821B45EC) with the constants sub_820653C8 sets.
  const RenderTuning& t = tuning;
  const float c = t.contrast, fc = t.foreground_contrast * c;
  SDL_GPUColorTargetInfo ct{};
  ct.texture = out;
  ct.clear_color = {0, 0, 0, 1};
  ct.load_op = SDL_GPU_LOADOP_CLEAR;
  ct.store_op = SDL_GPU_STOREOP_STORE;
  rp = SDL_BeginGPURenderPass(cmd, &ct, 1, nullptr);
  const Rect r{float((out_w - pw) / 2), float((out_h - ph) / 2), float(pw), float(ph)};
  const SDL_GPUViewport viewport{r.x, r.y, r.w, r.h, 0.0f, 1.0f};
  SDL_SetGPUViewport(rp, &viewport);
  SDL_BindGPUGraphicsPipeline(rp, composite_pipeline(format));
  const SDL_GPUTextureSamplerBinding bindings[4] = {
      {layer_a_.tex, sampler_}, {chain_a_.temp[0].tex, sampler_}, {layer_b_.tex, sampler_},
      {chain_b_.temp[0].tex, sampler_}};
  SDL_BindGPUFragmentSamplers(rp, 0, bindings, 4);
  const float inv_exposure = 1.0f / t.exposure;
  struct {
    Rect rect;
    float c0[4], c1[4], c2[4], c3[4];
    std::array<float, 256> ramp;
  } u{r,
      {c, t.bloom_bg * t.bloom_level * c, 0, 0},
      {fc, t.bloom_fg * t.bloom_level * t.foreground_contrast * c, 0, 0},
      {inv_exposure, inv_exposure, inv_exposure, inv_exposure},
      {t.shadow_factor, t.shadow_add, t.shadow_fudge, 0},
      display_gamma_ramp(t.display, t.display_power)};
  SDL_PushGPUFragmentUniformData(cmd, 0, &u, sizeof(u));
  SDL_DrawGPUPrimitives(rp, 3, 1, 0, 0);
  SDL_EndGPURenderPass(rp);
}

}  // namespace geow2

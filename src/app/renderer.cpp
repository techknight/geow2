#include "app/renderer.h"

#include <cmath>
#include <cstring>

#include "extract/bytes.h"
#include "game/bloom_weights.h"

namespace fs = std::filesystem;

namespace geow2 {
namespace {

constexpr SDL_GPUTextureFormat kLayerFormat = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
constexpr float kAspect = 1.77777779f;  // dat_82002DB0

// The back buffer of the original is 1280 x 720, or 1920 x 1080 on a
// high-definition output (sub_8205D538); in that mode the bloom chain
// starts with one more halving step (sub_82064640, flag dat_821A1D68).
constexpr uint32_t kStandardHeight = 720;

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

SDL_GPUGraphicsPipeline* line_pipeline(SDL_GPUDevice* dev, SDL_GPUShader* vs, SDL_GPUShader* fs) {
  // Blend 0x1010101 (sub_820679A8): ONE + ONE for colour and alpha.
  SDL_GPUColorTargetDescription target{};
  target.format = kLayerFormat;
  target.blend_state.enable_blend = true;
  target.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
  target.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
  target.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
  target.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
  target.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
  target.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
  SDL_GPUVertexBufferDescription vb{};
  vb.slot = 0;
  vb.pitch = sizeof(LineVertex);
  vb.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
  SDL_GPUVertexAttribute attrs[2]{};
  attrs[0] = {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 0};
  attrs[1] = {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 16};
  SDL_GPUGraphicsPipelineCreateInfo info{};
  info.vertex_shader = vs;
  info.fragment_shader = fs;
  info.vertex_input_state.vertex_buffer_descriptions = &vb;
  info.vertex_input_state.num_vertex_buffers = 1;
  info.vertex_input_state.vertex_attributes = attrs;
  info.vertex_input_state.num_vertex_attributes = 2;
  info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
  info.target_info.color_target_descriptions = &target;
  info.target_info.num_color_targets = 1;
  return SDL_CreateGPUGraphicsPipeline(dev, &info);
}

SDL_GPUGraphicsPipeline* screen_pipeline(SDL_GPUDevice* dev, SDL_GPUShader* vs, SDL_GPUShader* fs,
                                         SDL_GPUTextureFormat format) {
  SDL_GPUColorTargetDescription target{};
  target.format = format;
  SDL_GPUGraphicsPipelineCreateInfo info{};
  info.vertex_shader = vs;
  info.fragment_shader = fs;
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

}  // namespace

bool Renderer::init(SDL_GPUDevice* device, const fs::path& dir) {
  device_ = device;
  line_vs_ = load_shader(device, dir / "line.vert.spv", SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
  body_fs_ = load_shader(device, dir / "line_body.frag.spv", SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 0);
  cap_fs_ = load_shader(device, dir / "line_cap.frag.spv", SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 0);
  full_vs_ = load_shader(device, dir / "fullscreen.vert.spv", SDL_GPU_SHADERSTAGE_VERTEX, 0, 0);
  downsample_fs_ = load_shader(device, dir / "downsample.frag.spv", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);
  blur_fs_ = load_shader(device, dir / "blur.frag.spv", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);
  combine_fs_ = load_shader(device, dir / "combine.frag.spv", SDL_GPU_SHADERSTAGE_FRAGMENT, 5, 1);
  composite_fs_ = load_shader(device, dir / "composite.frag.spv", SDL_GPU_SHADERSTAGE_FRAGMENT, 4, 1);
  if (!line_vs_ || !body_fs_ || !cap_fs_ || !full_vs_ || !downsample_fs_ || !blur_fs_ || !combine_fs_ ||
      !composite_fs_)
    return false;
  body_ = line_pipeline(device, line_vs_, body_fs_);
  caps_ = line_pipeline(device, line_vs_, cap_fs_);
  downsample_ = screen_pipeline(device, full_vs_, downsample_fs_, kLayerFormat);
  blur_ = screen_pipeline(device, full_vs_, blur_fs_, kLayerFormat);
  combine_ = screen_pipeline(device, full_vs_, combine_fs_, kLayerFormat);
  // Reads go through texelFetch; the sampler only has to exist.
  SDL_GPUSamplerCreateInfo si{};
  si.min_filter = SDL_GPU_FILTER_NEAREST;
  si.mag_filter = SDL_GPU_FILTER_NEAREST;
  si.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
  si.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  si.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  sampler_ = SDL_CreateGPUSampler(device, &si);
  if (!body_ || !caps_ || !downsample_ || !blur_ || !combine_ || !sampler_) {
    SDL_Log("cannot create the render pipelines: %s", SDL_GetError());
    return false;
  }
  return true;
}

void Renderer::release() {
  if (!device_) return;
  resize(0, 0);
  for (SDL_GPUGraphicsPipeline* p : {body_, caps_, downsample_, blur_, combine_, composite_})
    if (p) SDL_ReleaseGPUGraphicsPipeline(device_, p);
  for (SDL_GPUShader* s :
       {line_vs_, body_fs_, cap_fs_, full_vs_, downsample_fs_, blur_fs_, combine_fs_, composite_fs_})
    if (s) SDL_ReleaseGPUShader(device_, s);
  if (sampler_) SDL_ReleaseGPUSampler(device_, sampler_);
  if (vertices_) SDL_ReleaseGPUBuffer(device_, vertices_);
  if (staging_) SDL_ReleaseGPUTransferBuffer(device_, staging_);
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

void Renderer::resize(uint32_t w, uint32_t h) {
  if (w == width_ && h == height_) return;
  for (Target* t : {&layer_a_, &layer_b_}) free_target(*t);
  for (Chain* c : {&chain_a_, &chain_b_}) {
    free_target(c->extra);
    free_target(c->first);
    for (Target& t : c->level) free_target(t);
    for (Target& t : c->temp) free_target(t);
  }
  width_ = w;
  height_ = h;
  if (w == 0 || h == 0) return;
  // Layer targets at the picture size (sub_82065270); bloom sizes as
  // sub_82064640 derives them.
  layer_a_ = make_target(w, h);
  layer_b_ = make_target(w, h);
  for (Chain* c : {&chain_a_, &chain_b_}) {
    uint32_t cw = w, ch = h;
    if (h > kStandardHeight) {
      cw = half(cw);
      ch = half(ch);
      c->extra = make_target(cw, ch);
    }
    cw = half(cw);
    ch = half(ch);
    c->first = make_target(cw, ch);
    for (size_t i = 0; i < c->level.size(); ++i) {
      cw = half(cw);
      ch = half(ch);
      c->level[i] = make_target(cw, ch);
      c->temp[i] = make_target(cw, ch);
    }
  }
}

void Renderer::upload(SDL_GPUCommandBuffer* cmd, const std::vector<LineVertex>& v) {
  const uint32_t size = uint32_t(v.size() * sizeof(LineVertex));
  if (size == 0) return;
  if (size > capacity_) {
    if (vertices_) SDL_ReleaseGPUBuffer(device_, vertices_);
    if (staging_) SDL_ReleaseGPUTransferBuffer(device_, staging_);
    SDL_GPUBufferCreateInfo b{SDL_GPU_BUFFERUSAGE_VERTEX, size, 0};
    vertices_ = SDL_CreateGPUBuffer(device_, &b);
    SDL_GPUTransferBufferCreateInfo t{SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, size, 0};
    staging_ = SDL_CreateGPUTransferBuffer(device_, &t);
    capacity_ = size;
  }
  void* p = SDL_MapGPUTransferBuffer(device_, staging_, true);
  std::memcpy(p, v.data(), size);
  SDL_UnmapGPUTransferBuffer(device_, staging_);
  SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
  SDL_GPUTransferBufferLocation src{staging_, 0};
  SDL_GPUBufferRegion dst{vertices_, 0, size};
  SDL_UploadToGPUBuffer(copy, &src, &dst, true);
  SDL_EndGPUCopyPass(copy);
}

void Renderer::pass(SDL_GPUCommandBuffer* cmd, const Target& dst, SDL_GPUGraphicsPipeline* pipe,
                    std::initializer_list<SDL_GPUTexture*> textures, const void* uniforms, uint32_t uniform_size) {
  SDL_GPUColorTargetInfo ct{};
  ct.texture = dst.tex;
  ct.load_op = SDL_GPU_LOADOP_DONT_CARE;
  ct.store_op = SDL_GPU_STOREOP_STORE;
  SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(cmd, &ct, 1, nullptr);
  SDL_BindGPUGraphicsPipeline(rp, pipe);
  SDL_GPUTextureSamplerBinding bindings[5]{};
  uint32_t n = 0;
  for (SDL_GPUTexture* t : textures) bindings[n++] = {t, sampler_};
  SDL_BindGPUFragmentSamplers(rp, 0, bindings, n);
  SDL_PushGPUFragmentUniformData(cmd, 0, uniforms, uniform_size);
  SDL_DrawGPUPrimitives(rp, 3, 1, 0, 0);
  SDL_EndGPURenderPass(rp);
}

void Renderer::bloom(SDL_GPUCommandBuffer* cmd, const Target& layer, Chain& c) {
  // sub_82064930 per level: downsample (an extra step first in high
  // definition, and the first step for level 0), then the blur across and
  // down; each level starts from the previous level's result.
  auto rect = [](const Target& t) { return Rect{0.0f, 0.0f, float(t.w), float(t.h)}; };
  const Target* src = &layer;
  for (Target* step : {&c.extra, &c.first}) {
    if (!step->tex) continue;
    const Rect r = rect(*step);
    pass(cmd, *step, downsample_, {src->tex}, &r, sizeof(r));
    src = step;
  }
  const std::array<float, 8> w = blur_weights(tuning.radius, tuning.precision);
  for (size_t i = 0; i < c.level.size(); ++i) {
    const Rect r = rect(c.level[i]);
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
  // sub_82064F88: the five levels combined at level 0's size.
  const std::array<float, 5> k = combine_weights(tuning.precision, tuning.bloom_falloff);
  struct {
    Rect rect;
    float c0[4], c1[4];
  } combine{rect(c.temp[0]), {k[0], k[1], k[2], 0}, {k[3], k[4], 0, 0}};
  pass(cmd, c.temp[0], combine_,
       {c.level[0].tex, c.level[1].tex, c.level[2].tex, c.level[3].tex, c.level[4].tex}, &combine,
       sizeof(combine));
}

void Renderer::draw_lines(SDL_GPUCommandBuffer* cmd, const Target& dst, const Mat4& vp, uint32_t body_first,
                          uint32_t body_count, uint32_t caps_first, uint32_t caps_count) {
  // Cleared to Render.ClearColour (0x821B39CC, default (0, 0, 0, 0)).
  SDL_GPUColorTargetInfo ct{};
  ct.texture = dst.tex;
  ct.clear_color = {0, 0, 0, 0};
  ct.load_op = SDL_GPU_LOADOP_CLEAR;
  ct.store_op = SDL_GPU_STOREOP_STORE;
  SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(cmd, &ct, 1, nullptr);
  if (body_count + caps_count > 0) {
    const SDL_GPUBufferBinding bind{vertices_, 0};
    SDL_BindGPUVertexBuffers(rp, 0, &bind, 1);
    SDL_PushGPUVertexUniformData(cmd, 0, vp.m, sizeof(vp.m));
    SDL_BindGPUGraphicsPipeline(rp, body_);  // preset 0x821B4694
    if (body_count) SDL_DrawGPUPrimitives(rp, body_count, 1, body_first, 0);
    SDL_BindGPUGraphicsPipeline(rp, caps_);  // preset 0x821B46B8
    if (caps_count) SDL_DrawGPUPrimitives(rp, caps_count, 1, caps_first, 0);
  }
  SDL_EndGPURenderPass(rp);
}

SDL_GPUGraphicsPipeline* Renderer::composite_pipeline(SDL_GPUTextureFormat format) {
  if (composite_ && composite_format_ == format) return composite_;
  if (composite_) SDL_ReleaseGPUGraphicsPipeline(device_, composite_);
  composite_ = screen_pipeline(device_, full_vs_, composite_fs_, format);
  composite_format_ = format;
  return composite_;
}

void Renderer::draw(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* out, SDL_GPUTextureFormat format, uint32_t out_w,
                    uint32_t out_h, const Mat4& vp, const LineBatch& layer_a, const LineBatch& layer_b) {
  // The 16:9 picture, letterboxed into the output.
  uint32_t pw = out_w, ph = uint32_t(float(out_w) / kAspect + 0.5f);
  if (ph > out_h) {
    ph = out_h;
    pw = uint32_t(float(out_h) * kAspect + 0.5f);
  }
  if (pw == 0 || ph == 0) return;
  resize(pw, ph);

  // Quads (four vertices each) as triangle pairs.
  std::vector<LineVertex> v;
  auto add = [&v](const std::vector<LineVertex>& quads) {
    const uint32_t first = uint32_t(v.size());
    for (size_t q = 0; q + 3 < quads.size(); q += 4)
      for (int i : {0, 1, 2, 0, 2, 3}) v.push_back(quads[q + i]);
    return std::pair{first, uint32_t(v.size()) - first};
  };
  const auto [a_body, a_body_n] = add(layer_a.body);
  const auto [a_caps, a_caps_n] = add(layer_a.caps);
  const auto [b_body, b_body_n] = add(layer_b.body);
  const auto [b_caps, b_caps_n] = add(layer_b.caps);
  upload(cmd, v);

  draw_lines(cmd, layer_a_, vp, a_body, a_body_n, a_caps, a_caps_n);
  draw_lines(cmd, layer_b_, vp, b_body, b_body_n, b_caps, b_caps_n);
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
  SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(cmd, &ct, 1, nullptr);
  const Rect r{float((out_w - pw) / 2), float((out_h - ph) / 2), float(pw), float(ph)};
  const SDL_GPUViewport viewport{r.x, r.y, r.w, r.h, 0.0f, 1.0f};
  SDL_SetGPUViewport(rp, &viewport);
  SDL_BindGPUGraphicsPipeline(rp, composite_pipeline(format));
  const SDL_GPUTextureSamplerBinding bindings[4] = {
      {layer_a_.tex, sampler_}, {chain_a_.temp[0].tex, sampler_}, {layer_b_.tex, sampler_},
      {chain_b_.temp[0].tex, sampler_}};
  SDL_BindGPUFragmentSamplers(rp, 0, bindings, 4);
  const float inv_exposure = 1.0f / t.exposure;
  const struct {
    Rect rect;
    float c0[4], c1[4], c2[4], c3[4];
  } u{r,
      {c, t.bloom_bg * t.bloom_level * c, 0, 0},
      {fc, t.bloom_fg * t.bloom_level * t.foreground_contrast * c, 0, 0},
      {inv_exposure, inv_exposure, inv_exposure, inv_exposure},
      {t.shadow_factor, t.shadow_add, t.shadow_fudge, 0}};
  SDL_PushGPUFragmentUniformData(cmd, 0, &u, sizeof(u));
  SDL_DrawGPUPrimitives(rp, 3, 1, 0, 0);
  SDL_EndGPURenderPass(rp);
}

}  // namespace geow2

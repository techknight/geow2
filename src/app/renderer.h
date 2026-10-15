#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <filesystem>
#include <vector>

#include "game/line_quads.h"
#include "game/math.h"

namespace geow2 {

// The render tuning variables the frame uses (registered by the static
// initialisers; variable address, default constant).
struct RenderTuning {
  float contrast = 0.2f;             // Render.Contrast 0x821B3AE0, dat_82000D5C
  float foreground_contrast = 1.0f;  // Render.ForegroundContrast 0x821B3C08, dat_820006C4
  float bloom_bg = 0.5f;             // Render.BloomBg 0x821B3A10, dat_82000724
  float bloom_fg = 0.5f;             // Render.BloomFg 0x821B3A44, dat_82000724
  float bloom_falloff = 1.25f;       // Render.BloomFallOff 0x821B3A78, dat_82000768
  float exposure = 2.0f;             // Render.Exposure 0x821B3B14, dat_820006FC
  float precision = 1.0f;            // Render.Precision 0x821B3B48, dat_820006C4
  float radius = 1.0f;               // Render.Radius 0x821B3BB0, dat_820006C4
  float shadow_factor = 0.0f;        // Render.ShadowFactor 0x821B3C3C, dat_820006CC
  float shadow_add = 0.15f;          // Render.ShadowAdd 0x821B3C70, dat_82000D58
  float shadow_fudge = 0.75f;        // Render.ShadowFudge 0x821B3CA4, dat_82000718
  // dat_821D39F0: scales both blooms; 0 at boot, 1.0 once the intro
  // sequence (sub_8207E9F0) is over.
  float bloom_level = 1.0f;
};

// The frame of sub_820653C8: two line layers drawn into high-range
// targets, each blurred into a five-level bloom, then composited.
class Renderer {
 public:
  bool init(SDL_GPUDevice* device, const std::filesystem::path& shader_dir);
  void release();

  // Draws into `out` (out_w x out_h, `format`), the 16:9 picture
  // letterboxed. Layer A is the background, layer B the foreground.
  void draw(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* out, SDL_GPUTextureFormat format, uint32_t out_w,
            uint32_t out_h, const Mat4& view_projection, const LineBatch& layer_a, const LineBatch& layer_b);

  RenderTuning tuning;

 private:
  struct Target {
    SDL_GPUTexture* tex = nullptr;
    uint32_t w = 0, h = 0;
  };
  struct Chain {
    Target extra, first;            // the high-definition step and the first downsample
    std::array<Target, 5> level{};  // each level's blurred result
    std::array<Target, 5> temp{};   // the horizontal blur pass; temp[0] ends up holding the bloom
  };

  void resize(uint32_t w, uint32_t h);
  Target make_target(uint32_t w, uint32_t h);
  void free_target(Target& t);
  void bloom(SDL_GPUCommandBuffer* cmd, const Target& layer, Chain& chain);
  void pass(SDL_GPUCommandBuffer* cmd, const Target& dst, SDL_GPUGraphicsPipeline* pipe,
            std::initializer_list<SDL_GPUTexture*> textures, const void* uniforms, uint32_t uniform_size);
  void draw_lines(SDL_GPUCommandBuffer* cmd, const Target& dst, const Mat4& view_projection, uint32_t body_first,
                  uint32_t body_count, uint32_t caps_first, uint32_t caps_count);
  void upload(SDL_GPUCommandBuffer* cmd, const std::vector<LineVertex>& v);
  SDL_GPUGraphicsPipeline* composite_pipeline(SDL_GPUTextureFormat format);

  SDL_GPUDevice* device_ = nullptr;
  SDL_GPUShader* line_vs_ = nullptr;
  SDL_GPUShader* body_fs_ = nullptr;
  SDL_GPUShader* cap_fs_ = nullptr;
  SDL_GPUShader* full_vs_ = nullptr;
  SDL_GPUShader* downsample_fs_ = nullptr;
  SDL_GPUShader* blur_fs_ = nullptr;
  SDL_GPUShader* combine_fs_ = nullptr;
  SDL_GPUShader* composite_fs_ = nullptr;
  SDL_GPUGraphicsPipeline* body_ = nullptr;
  SDL_GPUGraphicsPipeline* caps_ = nullptr;
  SDL_GPUGraphicsPipeline* downsample_ = nullptr;
  SDL_GPUGraphicsPipeline* blur_ = nullptr;
  SDL_GPUGraphicsPipeline* combine_ = nullptr;
  SDL_GPUGraphicsPipeline* composite_ = nullptr;
  SDL_GPUTextureFormat composite_format_ = SDL_GPU_TEXTUREFORMAT_INVALID;
  SDL_GPUSampler* sampler_ = nullptr;
  SDL_GPUBuffer* vertices_ = nullptr;
  SDL_GPUTransferBuffer* staging_ = nullptr;
  uint32_t capacity_ = 0;

  uint32_t width_ = 0, height_ = 0;
  Target layer_a_, layer_b_;
  Chain chain_a_, chain_b_;
};

}  // namespace geow2

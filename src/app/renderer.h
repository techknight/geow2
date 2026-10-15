#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <filesystem>
#include <vector>

#include "game/display_gamma.h"
#include "game/light_map.h"
#include "game/line_quads.h"
#include "game/math.h"
#include "game/stars.h"

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
  // MapLight.* (light map, sub_82065930).
  float light_feedback_mul = 0.75f;  // 0x821B3CD8, dat_82000718
  float light_feedback_add = 0.5f;   // 0x821B3D0C, dat_82000724
  float light_contrast = 0.25f;      // 0x821B3D40, dat_82000720
  float light_bloom = 0.75f;         // 0x821B3D74, dat_82000718
  float light_precision = 1.0f;      // 0x821B3DA8, dat_820006C4
  float light_radius = 2.0f;         // MapLight.BloomRadius 0x821B3DDC, dat_820006FC
  // The display the console's output gamma ramp is built for
  // (VdGetCurrentDisplayGamma; DIFFERENCES.md).
  DisplayGamma display = DisplayGamma::kBt709;
  float display_power = 2.22222233f;
};

// The frame of sub_820653C8: the light map the grid is lit by, two layers
// (background with the grid, foreground lines) drawn into high-range
// targets, each blurred into a five-level bloom, then composited.
class Renderer {
 public:
  bool init(SDL_GPUDevice* device, const std::filesystem::path& shader_dir);
  void release();

  // The light map's update for one simulation step (sub_82065930): warp
  // last step's map along the grid, add this step's light sprites, bloom,
  // feed back. The original renders one frame per step; the port runs
  // this once per step whatever the display rate.
  void update_light_map(SDL_GPUCommandBuffer* cmd, const std::vector<GridVertex>& warp_mesh,
                        const std::vector<LightVertex>& sprites);

  // Draws into `out` (out_w x out_h, `format`), the 16:9 picture
  // letterboxed. `grid` is null while the grid is faded out.
  void draw(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* out, SDL_GPUTextureFormat format, uint32_t out_w,
            uint32_t out_h, const Mat4& view_projection, const std::vector<StarVertex>& stars,
            const std::vector<GridVertex>* grid, const std::vector<GridVertex>& walls,
            const std::vector<LineVertex>& particles, const LineBatch& linger, const LineBatch& foreground,
            const LineBatch& ui,
            const Mat4& ui_transform);

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
  struct Buffer {
    SDL_GPUBuffer* buf = nullptr;
    SDL_GPUTransferBuffer* staging = nullptr;
    uint32_t capacity = 0;
  };

  void resize(uint32_t w, uint32_t h);
  Target make_target(uint32_t w, uint32_t h);
  void free_target(Target& t);
  void make_chain(Chain& c, uint32_t w, uint32_t h, bool extra);
  void free_chain(Chain& c);
  void blur_levels(SDL_GPUCommandBuffer* cmd, const Target& src, Chain& chain, float radius, float precision);
  void bloom(SDL_GPUCommandBuffer* cmd, const Target& layer, Chain& chain);
  void pass(SDL_GPUCommandBuffer* cmd, const Target& dst, SDL_GPUGraphicsPipeline* pipe,
            std::initializer_list<SDL_GPUTexture*> textures, const void* uniforms, uint32_t uniform_size);
  void upload(SDL_GPUCommandBuffer* cmd, Buffer& b, SDL_GPUBufferUsageFlags usage, const void* data, uint32_t size);
  void quad_indices(SDL_GPUCommandBuffer* cmd, uint32_t quads);
  SDL_GPUGraphicsPipeline* composite_pipeline(SDL_GPUTextureFormat format);

  SDL_GPUDevice* device_ = nullptr;
  std::vector<SDL_GPUShader*> shaders_;
  SDL_GPUShader* full_vs_ = nullptr;
  SDL_GPUShader* composite_fs_ = nullptr;
  SDL_GPUGraphicsPipeline* body_ = nullptr;
  SDL_GPUGraphicsPipeline* caps_ = nullptr;
  SDL_GPUGraphicsPipeline* grid_ = nullptr;
  SDL_GPUGraphicsPipeline* warp_ = nullptr;
  SDL_GPUGraphicsPipeline* sprites_ = nullptr;
  SDL_GPUGraphicsPipeline* stars_ = nullptr;
  SDL_GPUGraphicsPipeline* walls_ = nullptr;
  SDL_GPUGraphicsPipeline* particles_ = nullptr;
  SDL_GPUGraphicsPipeline* ui_body_ = nullptr;
  SDL_GPUGraphicsPipeline* ui_caps_ = nullptr;
  SDL_GPUGraphicsPipeline* downsample_ = nullptr;
  SDL_GPUGraphicsPipeline* blur_ = nullptr;
  SDL_GPUGraphicsPipeline* combine_ = nullptr;
  SDL_GPUGraphicsPipeline* light_combine_ = nullptr;
  SDL_GPUGraphicsPipeline* feedback_ = nullptr;
  SDL_GPUGraphicsPipeline* composite_ = nullptr;
  SDL_GPUTextureFormat composite_format_ = SDL_GPU_TEXTUREFORMAT_INVALID;
  SDL_GPUSampler* sampler_ = nullptr;         // reads go through texelFetch
  SDL_GPUSampler* sprite_sampler_ = nullptr;  // bilinear, clamped
  SDL_GPUTexture* sprite_ = nullptr;          // dat_821A1D60
  SDL_GPUTexture* star_sprite_ = nullptr;     // dat_821B4768

  Buffer lines_, ui_lines_, linger_lines_, particle_vertices_, grid_vertices_, wall_vertices_, warp_vertices_, sprite_vertices_, star_vertices_, quad_index_,
      warp_index_;
  uint32_t quad_index_quads_ = 0;
  uint32_t warp_index_count_ = 0;

  uint32_t width_ = 0, height_ = 0;
  Target layer_a_, layer_b_;
  Chain chain_a_, chain_b_;
  // Light map (150 x 100): the final map (0x821B3520) kept from step to
  // step, the warped previous map (0x821B3524), this step's sprites and
  // their combined bloom, and the bloom chain (table 0x821B3730).
  Target light_, light_warped_, light_new_, light_combined_;
  Chain light_chain_;
  bool light_cleared_ = false;
};

}  // namespace geow2

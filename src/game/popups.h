#pragma once

#include <array>
#include <string>

#include "game/font.h"

namespace geow2 {

// The numbers that linger where something scored (Render.Layer.Linger):
// small ones (sub_8205A668: a ring of 64, each shown for 32 frames,
// shrinking and fading) and big ones (sub_8205A738: 40 slots, each shown
// for its duration, shrinking over its last second), drawn with the UI
// font in world space (sub_8205A8F0).
class Popups {
 public:
  // sub_8205A668(&pos, size, colour index, text): dropped when 64 are on
  // screen. The text is at most 15 characters.
  void add_small(float x, float y, float size, int colour, const std::string& text);
  // sub_8205A738(&pos, duration, colour index, text): in the first free
  // slot (at most 31 characters).
  void add_big(float x, float y, float duration, int colour, const std::string& text);
  // sub_8205A8F0 (render set-up) into `out`, with the world camera.
  void draw(const Font& font, LineBatch& out) const;
  // sub_8205A7F0 (render worker): the frame's small ones join the ring and
  // the oldest frame's leave it; big ones age.
  void step(float dt);

 private:
  struct Small {
    float x = 0, y = 0;
    std::string text;
    float size = 0;
    int colour = 0;
  };
  struct Big {
    std::string text;
    float x = 0, y = 0, duration = 0, time = 0;
    int colour = 0;
  };
  std::array<Small, 64> small_{};   // 0x821A06B0
  std::array<int, 32> frames_{};    // 0x821A0620: small ones added per frame
  int frame_ = 0;                   // dat_821A06A0
  int pending_ = 0;                 // dat_821A06A4
  int write_ = 0;                   // dat_821A06A8
  int first_ = 0;                   // dat_821A06AC
  int budget_ = 64;                 // dat_821825B0
  std::array<Big, 40> big_{};       // 0x821A0EB0
};

}  // namespace geow2

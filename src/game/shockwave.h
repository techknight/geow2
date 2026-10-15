#pragma once

#include "game/world.h"

namespace geow2 {

// The expanding blast (sub_82056918, vtable 0x82002320, type code 0xF):
// grows from its centre at `speed` units a second up to `max_radius`,
// destroying what it reaches (sub_82056850 / sub_82055DA0); drawn as four
// rings (sub_82055BA8). The bomb's is kind 0 (400, 533.2).
class Shockwave : public Entity {
 public:
  Shockwave(World& w, float max_radius, float speed, float px, float py, int kind);
  void update(World& w) override;             // 0x82056A18
  void draw(World& w, LineBatch& out) override;  // sub_82055BA8

  float max_radius = 0, speed = 0, r = 0;  // +0x6C, +0x70, +0x74
  int kind = 0;                            // +0x68
  size_t player = 0;                       // +0x48: the player it is credited to

 private:
  void blast(World& w);  // sub_82056850
};

}  // namespace geow2

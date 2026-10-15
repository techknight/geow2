#pragma once

#include "game/world.h"

namespace geow2 {

// The wanderer (spawn type 1, type code 0x12, unit "wanderer"; constructor
// sub_82059660, vtable 0x8200269C): a spinning pinwheel that drifts at
// about 50 units a second, leaning a little toward the nearest player,
// bouncing off the walls and keeping apart from other wanderers.
class Wanderer : public Entity {
 public:
  Wanderer(World& w, float px, float py);
  void update(World& w) override;        // sub_82059838
  void wall(World& w, Vec2 n) override;  // 0x82059618
  Mat4 custom_placement(World& w) override;  // sub_820599B0

  float spin = 0;  // +0x80: heading change per step
};

}  // namespace geow2

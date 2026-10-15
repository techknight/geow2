#pragma once

#include "game/world.h"

namespace geow2 {

// What is left of the enemy that killed a player (sub_82044A50, vtable
// 0x82001824, type code 7): the enemy's model frozen where it was, blinking
// white for a second while the rest of the field explodes.
class KillerGhost : public Entity {
 public:
  KillerGhost(World& w, Entity& killer);
  void update(World& w) override;                 // sub_82044820
  void draw(World& w, LineBatch& out) override;   // sub_82044918

  Mat4 world{};                 // +0x98: the killer's placement
  const ModelGeometry* model = nullptr;  // +0x90
  int killer_type = 0;          // +0x8C
  float base_colour[4] = {1, 1, 1, 1};  // +0x64: the killer's colour
  float draw_colour[4] = {1, 1, 1, 1};  // +0x74
  float time = 1.0f;            // +0x84
  int blink = 0;                // +0x88
  bool shown = false;           // +0x40 holds the shape table (drawn) or 0
  int killer_points = 0;        // +0x60: the copied shape table's size
};

// sub_82044FA8: the killer gives way to its ghost and is removed. (Types
// 3, 0xD/0xE, 0x14/0x15 and 0x18 have their own cases: they come with
// those enemies.)
void replace_killer(World& w, Entity& killer);

}  // namespace geow2

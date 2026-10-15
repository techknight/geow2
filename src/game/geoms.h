#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "game/world.h"

namespace geow2 {

// The geoms' line shapes, read from the user's image: points in pairs.
struct GeomShapes {
  std::vector<Vec2> small;  // 0x82181A58, 8 points: geoms worth 1 and 2
  std::vector<Vec2> large;  // 0x82181A98, 24 points: worth 5 and 10
  std::vector<Vec2> other;  // 0x82181A24, 6 points
  static GeomShapes from_image(std::span<const uint8_t> image);
};

// A geom (the game calls them motes; sub_8204DC48, vtable 0x82001D80, type
// code 0x22, list 0x82181450): dropped by kills, it drifts and spins, blinks
// over its last second and goes; a player close enough pulls it in and
// picks it up for its value in multiplier.
class Geom : public Entity {
 public:
  Geom(World& w, const Entity& victim, int value);
  void light(World& w, std::vector<LightVertex>& out) override;  // sub_8204D840
  void draw(World& w, LineBatch& out) override;                  // sub_8204D0B0
  void update(World& w) override;                                // sub_8204D8E0
  void wall(World& w, Vec2 n) override;                          // sub_8204D0D0
  // Vtable slot 6 (sub_8204D118): picked up by player `p`.
  void collect(World& w, size_t p);

  int value = 0;           // +0x6C
  float age = 0.0f;        // +0x68
  float life = 0.0f;       // +0x74
  bool hidden = false;     // +0x70: blinked out
  float base_colour[4] = {0, 0, 0, 0};  // +0x78
  int collector = -1;      // +0x48: the player pulling it in
  const std::vector<Vec2>* shape = nullptr;  // +0x40 / +0x3C
};

// sub_8204E108: the geoms a kill drops (kinds 0 and 2 drop none), by the
// victim's type code and the mode.
void drop_geoms(World& w, const Entity& victim, const Killer& k);

// sub_8204D410 (after the shots): each geom is pulled toward the nearest
// player within reach (radii plus Unit.Mote.MagnetRange) and picked up on
// contact.
void collect_geoms(World& w);

}  // namespace geow2

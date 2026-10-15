#pragma once

#include "game/grid.h"
#include "game/world.h"

namespace geow2 {

// Unit.Hill tuning (registration addresses; defaults).
constexpr float kHillTime = 3.5f;     // Unit.Hill.Time (0x8219EE00)
constexpr float kHillRadius = 40.0f;  // Unit.Hill.Radius (0x8219EE34)
constexpr bool kHillSafeZone = true;  // Unit.Hill.SafeZone (0x8219EE68)

// King's zone (type code 0x1C, unit "hill"; constructor sub_8204AD38,
// vtable 0x82001BE4, list 0x82181438). It grows to its radius and waits;
// once a player touches it, it turns green, then red, hands over to a new
// zone elsewhere and shrinks away. Units other than players are pushed
// out of it.
class Hill : public Entity {
 public:
  Hill(World& w, float px, float py);
  void draw(World& w, LineBatch& out) override;    // sub_8204AC60
  void update(World& w) override;                  // sub_8204AEE8
  void wall(World& w, Vec2 n) override;            // sub_8204A8F0
  Mat4 custom_placement(World& w) override;        // sub_8204A7A8
  void kill(World& w, const Killer& k) override;   // sub_8204A938: removed

  int state = 0;            // +0x8C: 0 waiting, 1 captured, 2 ending, 3 shrinking
  float timer = kHillTime;  // +0x90
};

// sub_8204A9C0: a place for a new zone, clear of the players (a third of a
// second ahead, by 1.5 radii) and of the zones (by 2.5 radii), in up to 32
// tries.
Vec2 hill_position(World& w);

// sub_8204A948: whether a circle overlaps any zone.
bool in_hill(const World& w, float radius, float x, float y);

// sub_8204D2E8 for one geom: pushed out of every zone it overlaps.
void hill_push_geom(World& w, Entity& geom);

// sub_8205B578's zone pushes: each zone's circle, strength 4.
void hill_grid_pushes(const World& w, std::vector<GridPush>& out);

}  // namespace geow2

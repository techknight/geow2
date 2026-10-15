#pragma once

#include "game/grid.h"
#include "game/world.h"

namespace geow2 {

// Unit.GravityWell tuning (registration addresses; defaults).
constexpr int kWellStartHits = 6;         // Unit.GravityWell.StartHits (0x8219EA94)
constexpr int kWellMaxHits = 12;          // Unit.GravityWell.MaxHits (0x8219EAC8)
constexpr float kWellIdleLight = 4.0f;    // Unit.GravityWell.IdleLightFactor (0x8219EB40)
constexpr float kWellAngryLight = 25.0f;  // Unit.GravityWell.AngryLightFactor (0x8219EB74)

// The gravity well (spawn type 5, type code 3, unit "gravitywell";
// constructor sub_820499E0, vtable 0x82001A28, list 0x82181408). Idle
// until first shot; then it pulls everything within 256 (bending shots),
// swallows enemies it touches and grows, and overloads past its maximum.
// Each shot takes a hit off and shoves everything away; at 0 it dies in a
// blast that sets off other wells.
class GravityWell : public Entity {
 public:
  GravityWell(World& w, float px, float py);
  void light(World& w, std::vector<LightVertex>& out) override;  // sub_82048740
  void draw(World& w, LineBatch& out) override;                  // sub_820490A0
  void update(World& w) override;                                // sub_82049370
  void wall(World& w, Vec2 n) override;                          // 0x82048528
  Mat4 custom_placement(World& w) override;                      // sub_82048918
  void hit_by_shot(World& w, const Killer& k) override;          // 0x82048A50
  void kill(World& w, const Killer& k) override;                 // sub_820497C0
  int special_points(const World& w) const override;
  int special_geoms() const override;

  int hits = kWellStartHits;  // +0x80
  bool active = false;        // +0x84
  bool ui = false;            // +0x85: the front end's decorative wells
  int hum = 0;                // +0x88
  float phase = 0.0f;         // +0x8C
  bool shoved = false;        // +0x90: shot this step
  bool swallowed = false;     // +0x91: swallowed something (or burst) this step
  float pulse = 0.0f;         // +0x94
  bool burst = false;         // +0x98
  int absorbed = 0;           // +0x9C

 private:
  void pull(World& w);   // sub_82048AA0
  void shove(World& w);  // sub_82048658
};

// What the wells feed the grid (sub_8205B578) and the particles
// (sub_820632F0).
void well_grid_lights(const World& w, std::vector<GridLight>& out);
void well_particle_forces(World& w);

}  // namespace geow2

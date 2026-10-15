#pragma once

#include "game/world.h"

namespace geow2 {

// Unit.Amoeba tuning (registration addresses; defaults).
constexpr float kCytoplasmDamp = 0.9f;               // Unit.Amoeba.CytoplasmDamp (0x8219DDD8)
constexpr float kCytoplasmTendToPosition = 0.5f;     // Unit.Amoeba.CytoplasmTendToPosition (0x8219DE0C)
constexpr float kCytoplasmTendToVelocity = 0.025f;   // Unit.Amoeba.CytoplasmTendToVelocity (0x8219DE40)
constexpr float kCytoplasmRadiusLow = 3.0f;          // Unit.Amoeba.CytoplasmRadiusLow (0x8219DE74)
constexpr float kCytoplasmRadiusHigh = 6.0f;         // Unit.Amoeba.CytoplasmRadiusHigh (0x8219DEA8)
constexpr float kCytoplasmPulsateRate = 20.0f;       // Unit.Amoeba.CytoplasmPulsateRate (0x8219DEDC)
constexpr float kCytoplasmSpawnTime = 0.25f;         // Unit.Amoeba.CytoplasmSpawnTime (0x8219DF98)
constexpr int kCytoplasmPerSpawn = 1;                // Unit.Amoeba.NoCytoplasmPerSpawn (0x8219DFCC)
constexpr int kCytoplasmAtStart = 40;                // Unit.Amoeba.NoCytoplasmAtStart (0x8219E000)
constexpr int kMaxCytoplasms = 40;                   // Unit.Amoeba.MaxCytoplasms (0x8219E034)

// The amoeba's nucleus (spawn type 11, type code 0x14, unit
// "amoebanucleus"; constructor sub_82041860, vtable 0x82001460): wanders,
// surrounded by cells that it buds again as they are shot away.
class Nucleus : public Entity {
 public:
  Nucleus(World& w, float px, float py);
  // The constructor's first cells, once the nucleus is in the object list
  // (they come after it).
  void start(World& w);
  void update(World& w) override;   // sub_82041AA8
  void wall(World& w, Vec2 n) override;  // 0x82041818
  void on_delete(World& w) override;     // sub_82046B08 with flag 0x4000

  int cells = 0;            // +0x80: live cells
  float bud_timer = 0.0f;   // +0x84

 private:
  void bud(World& w, float spread, bool fade_in);
};

// A cell of an amoeba (type code 0x15, unit "amoebacytoplasm"; constructor
// sub_82041108, vtable 0x82001350): held to its nucleus by a spring,
// pulsing in size, kept apart from the other cells; it dies with the
// nucleus.
class Cytoplasm : public Entity {
 public:
  Cytoplasm(World& w, float px, float py, Nucleus* n);
  void update(World& w) override;            // sub_82041450
  void wall(World& w, Vec2 n) override;      // 0x820410C0
  Mat4 custom_placement(World& w) override;  // sub_82041318
  void on_delete(World& w) override;         // sub_82041790

  Nucleus* nucleus = nullptr;  // +0x48 (cleared when the nucleus is deleted)
  float age = 0.0f;            // +0x80

 private:
  void keep_apart(World& w, float reach, float strength);  // sub_82046810
};

}  // namespace geow2

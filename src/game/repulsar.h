#pragma once

#include "game/world.h"

namespace geow2 {

// Unit.Repulsar tuning (registration addresses; defaults).
constexpr float kRepulsarSpeed = 36.0f;       // Unit.Repulsar.Speed (0x8219FAD8)
constexpr float kRepulsarShove = 0.15f;       // Unit.Repulsar.Shove (0x8219FB0C)
constexpr float kRepulsarDizzyFactor = 0.01f; // Unit.Repulsar.DizzyFactor (0x8219FB40)
constexpr float kRepulsarDizzyScale = 1.0f;   // Unit.Repulsar.DizzyScale (0x8219FB74)
constexpr float kRepulsarDizzyTime = 0.1f;    // Unit.Repulsar.DizzyTime (0x8219FBA8)

// The repulsar (spawn type 6, type code 0xB, unit "repulsar"; constructor
// sub_82054988, vtable 0x82002070): aims at its target, then charges at
// where it will be; shielded except from behind, a shot elsewhere only
// shoves it and leaves it dizzy.
class Repulsar : public Entity {
 public:
  Repulsar(World& w, float px, float py);
  void update(World& w) override;                      // sub_82053DF8
  void wall(World& w, Vec2 n) override;                // sub_82054138
  void hit_by_shot(World& w, const Killer& k) override;  // sub_82054218
  void kill(World& w, const Killer& k) override;       // sub_82053D28
  void draw(World& w, LineBatch& out) override;        // sub_82054558

  int state = 0;          // +0x8C: 0 aim, 1 charge, 2 recover
  float timer = 0.0f;     // +0x90
  float aim_time = 0.0f;  // +0x94: aim needed before charging
  float flash = 0.0f;     // +0x98
  float dizzy = 0.0f;     // +0x9C
};

// sub_8205E8B8: where a body leaving `a` (velocity `av`) at `speed` meets
// `b` moving at `bv` (relative velocities), the time scaled by `k`; `b`
// itself when it cannot.
Vec2 lead_point(float speed, float k, Vec2 a, Vec2 av, Vec2 b, Vec2 bv);

}  // namespace geow2

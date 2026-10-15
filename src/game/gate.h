#pragma once

#include <vector>

#include "game/bullets.h"
#include "game/world.h"

namespace geow2 {

// One end of a gate (spawn type 16, type code 0x18, unit "gate";
// constructor sub_820482C0, vtable 0x820018F4, list 0x8218142C). Two ends
// spin around each other at the gate's length with a beam between them:
// shots bounce off the beam and only flash the ends; a player crossing
// the beam sets off two blasts that destroy what is around them, and the
// gate with them.
class GateEnd : public Entity {
 public:
  GateEnd(World& w, float px, float py);
  void update(World& w) override;                        // sub_820473D0
  void wall(World& w, Vec2 n) override;                  // 0x82047300
  Mat4 custom_placement(World& w) override;              // sub_82047C00
  void draw(World& w, LineBatch& out) override;          // sub_82047DF8 (+ the beam, sub_82047AA8)
  void hit_by_shot(World& w, const Killer& k) override;  // 0x82047380
  void kill(World& w, const Killer& k) override;         // sub_820480A8
  void on_delete(World& w) override;                     // sub_82048488
  int special_points(const World& w) const override;

  bool leader = false;            // +0x8C
  GateEnd* partner = nullptr;     // +0x90
  float flash = 0.0f;             // +0x94
  float beam_flash = 0.0f;        // +0x98 (leader)
  float wobble_x = 0, wobble_y = 0;  // +0x9C / +0xA0 (leader)
  float prev_x = 0, prev_y = 0;   // +0xA4 / +0xA8: position at the start of the step
  float sign = 1.0f;              // +0xAC: spin direction
};

// Spawn type 16 (sub_8203BEE8 case 0xF): the pair around `pos`, the
// leader returned.
Entity* spawn_gate(World& w, float px, float py);

// sub_820476F0 (after the shots): players crossing a beam (or within 6 of
// it, between the ends) set the gate off.
void gate_crossings(World& w);

// sub_82051B18 (before a shot's hit test): a shot crossing a beam this
// step bounces off it.
void bounce_off_beams(World& w, Bullet& b);

}  // namespace geow2

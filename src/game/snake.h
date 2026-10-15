#pragma once

#include "game/world.h"

namespace geow2 {

class SnakeSegment;

// The snake's head (spawn type 8, type code 0xD, unit "snakehead";
// constructor sub_82057790, vtable 0x8200241C): swims toward its target
// with a wiggle, trailing 23 segments. Shots kill it; its segments only
// flash.
class SnakeHead : public Entity {
 public:
  SnakeHead(World& w, float px, float py);
  // The rest of the constructor: the 23 segments, tail tip first, all at
  // the head's position (they join the object list after the head).
  void build_segments(World& w);
  void update(World& w) override;                 // sub_82057B60
  void wall(World& w, Vec2 n) override;           // 0x82057700
  void kill(World& w, const Killer& k) override;  // sub_82057CE0
  void on_delete(World& w) override;              // sub_82057DA0
  // sub_82057748: every segment lets go of the head; `drop` is whether
  // they will drop geoms when they die.
  void detach_all(bool drop);

  float phase = 0.0f;                 // +0x88: wiggle
  SnakeSegment* first = nullptr;      // +0x94: the segment next to the head
  int segments = 0;                   // +0x90
};

// A snake segment (type code 0xE, unit "snakebody"; constructor
// sub_820574A0, vtable 0x8200238C, list 0x82181414): follows the one
// ahead; attached, it kills players it touches and passes other kills to
// the head; detached, it dies after a delay.
class SnakeSegment : public Entity {
 public:
  SnakeSegment(World& w, SnakeHead& head, SnakeSegment* behind, int index, float length, float brightness,
               float delay, float radius_factor);
  void update(World& w) override;                        // sub_82056CD8
  void draw(World& w, LineBatch& out) override;          // sub_82056FA0
  void wall(World& w, Vec2 n) override;                  // sub_82056AD0
  Mat4 custom_placement(World& w) override;              // sub_82056B48
  void hit_by_shot(World& w, const Killer& k) override;  // sub_82056B08
  void kill(World& w, const Killer& k) override;         // sub_82057240
  void on_delete(World& w) override;                     // sub_82057600
  int special_geoms() const override { return index % 4 == 0 ? 1 : 0; }

  SnakeHead* head = nullptr;      // +0x48 (the head, while attached)
  SnakeSegment* ahead = nullptr;  // +0xA0: toward the head (none: the head)
  SnakeSegment* behind = nullptr;  // +0xA4: toward the tail
  float delay = 0.0f;             // +0x8C: life left once detached
  float flash = 0.0f;             // +0x90: hit flash
  float brightness = 1.0f;        // +0x94
  bool drop = false;              // +0x98: drops geoms when it dies
  int index = 0;                  // +0x9C
};

// The head a segment is attached to, or null (the snake's case of
// sub_82044FA8).
Entity* snake_head_of(Entity& e);

}  // namespace geow2

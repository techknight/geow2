#pragma once

#include "game/world.h"

namespace geow2 {

// The grunt (spawn type 2, type code 2, unit "grunt"; constructor
// sub_82049D40, vtable 0x82001B1C): homes in on its target with a pull
// that keeps growing, pulsing as it goes.
class Grunt : public Entity {
 public:
  Grunt(World& w, float px, float py);
  void update(World& w) override;            // sub_82049E60
  void wall(World& w, Vec2 n) override;      // sub_82049E18
  Mat4 custom_placement(World& w) override;  // sub_82049FA0

  float pull = 9.0f;   // +0x80 (dat_82001B14)
  float phase = 0.0f;  // +0x84
  float pulse = 0.0f;  // +0x88: cos(phase)
};

// The duck (spawn type 22, type code 0x21, unit "duck"; constructor
// sub_82044028, vtable 0x820017B4): hops one square step every
// Unit.Duck.Tick seconds, rolling a half turn each hop, never straight
// back, half its hops along the main axis toward the nearest player.
class Duck : public Entity {
 public:
  Duck(World& w, float px, float py);
  void update(World& w) override;            // sub_82044330
  void wall(World& w, Vec2 n) override;      // sub_82043FE0
  Mat4 custom_placement(World& w) override;  // sub_82044168

  float timer = 0.0f;                         // +0x80
  float heading_target = 0.0f;                // +0x84
  float heading_speed = 0.0f;                 // +0x88
  float roll_y = 0, roll_y_target = 0, roll_y_step = 0;  // +0x8C, +0x90, +0x94
  float roll_x = 0, roll_x_target = 0, roll_x_step = 0;  // +0x98, +0x9C, +0xA0
  float dir_x = 0, dir_y = 0;                 // +0xA4, +0xA8
  int forbidden = -1;                         // +0xAC: the way back
  bool flip = false;                          // +0xB0
};

// The spinner (spawn type 4, type code 9, unit "spinner"; constructor
// sub_82058570, vtable 0x8200255C): homes in, wobbling; shot by a player,
// it splits into three tiny spinners.
class Spinner : public Entity {
 public:
  explicit Spinner(World& w, float px, float py);
  void update(World& w) override;            // sub_820587C0
  void wall(World& w, Vec2 n) override;      // sub_82058528
  Mat4 custom_placement(World& w) override;  // sub_82058620
  void kill(World& w, const Killer& k) override;  // sub_820588F0

  float phase = 0.0f;  // +0x80
};

// A spinner's child (type code 0xA, unit "tinyspinner"; constructor
// sub_82059158, vtable 0x82002648): flung out at 300 a second, harmful at
// once, then swimming in circles.
class TinySpinner : public Entity {
 public:
  TinySpinner(World& w, float angle, float speed, float px, float py, float vx0, float vy0);
  void update(World& w) override;                // sub_82059320
  void wall(World& w, Vec2 n) override;          // sub_82059110
  void draw(World& w, LineBatch& out) override;  // sub_82059408

  float spin = 0.0f;  // +0x80
  // The constructor's move along the launch (applied after the object joins
  // the spatial hash at the parent's position).
  float move_x = 0.0f, move_y = 0.0f;
};

}  // namespace geow2

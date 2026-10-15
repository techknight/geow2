#pragma once

#include "game/world.h"

namespace geow2 {

// A multiplayer power-up item (constructor sub_8204C530, type 0x10, vtable
// 0x82001C88, its own list 0x821813CC): the unit "credit" drifting and
// spinning, fading in, gone after a minute. Not in the spatial hash, so
// shots and blasts pass it by; players pick it up on contact
// (sub_8204B770, in the game).
class PowerUp : public Entity {
 public:
  // At (x, y): heading a random turn (one draw), velocity a random vector
  // of 16 (two draws, y first), radius 7.5, colour (2.4, 2.1, 0, 3) until
  // the first draw, fading in.
  PowerUp(World& w, float px, float py, int kind);

  // sub_8204C070: ages (after a minute, line debris and removal), slows
  // above 32 a second (times 0.985 a step) and turns 5 radians a second.
  void update(World& w) override;
  // sub_8204B478: the velocity reflected off the wall.
  void wall(World& w, Vec2 n) override;
  // The first half of sub_8204BC88: the colour from the colour wheel at
  // its position (turned by its heading plus pi), and a light sprite of
  // 35. (The port takes the colour here, where the frame's lights are
  // gathered every step.)
  void light(World& w, std::vector<LightVertex>& out) override;
  // The rest of sub_8204BC88: the model's part 0 in that colour, tilted
  // along its velocity (as the units' TiltVelocity style), or the two
  // growing ghosts while fading in.
  void draw(World& w, LineBatch& out) override;
  // The end of sub_8204C2A8 (vtable slot 6): stopped, line debris from its
  // edge toward the player `p` with the player's velocity, removed.
  void collected(World& w, const PlayerTarget& p);

  int kind = 3;           // +0x68: 3 the random power-up (0 bomb and 1 life are never built)
  float age = 0.0f;       // +0x6C
  const UnitInfo* credit = nullptr;  // +0x70: the unit "credit" (0x82001C80)

 private:
  Mat4 tilt(World& w) const;
};

}  // namespace geow2

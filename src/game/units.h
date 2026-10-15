#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "data/flask.h"
#include "game/effects.h"
#include "game/game_math.h"
#include "game/math.h"
#include "game/model_geometry.h"

namespace geow2 {

// Unit.style: how an entity's model is placed (sub_82045CF0).
enum class UnitStyle { kNone = 0, kTiltVelocity = 1, kTiltPlayer = 2, kCustom = 3 };

// A Unit record of Data.bin as the game uses it (looked up by name,
// sub_82031D40): its model's line geometry, placement style, up vector
// (+0x10), tint (+0x1C), glow colour (+0x2C) and glow size (+0x3C).
struct UnitInfo {
  std::string name;
  ModelGeometry model;
  bool has_model = false;
  UnitStyle style = UnitStyle::kNone;
  Vec4 up{0, 0, 0, 0};
  float tint[4] = {1, 1, 1, 1};
  float glow[4] = {0, 0, 0, 0};
  float glow_size = 0.0f;
  UnitEffect killed;  // +0x40: triggered on death (sub_82030658)
};

class Units {
 public:
  // Decodes every Unit record and builds its model's geometry.
  static Units load(const Flask& db, const GameMath& gm);
  const UnitInfo* find(std::string_view name) const;

 private:
  std::vector<UnitInfo> units_;
};

}  // namespace geow2

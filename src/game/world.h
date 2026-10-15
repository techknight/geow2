#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "game/game_math.h"
#include "game/light_map.h"
#include "game/line_quads.h"
#include "game/math.h"
#include "game/player_motion.h"
#include "game/rng.h"
#include "game/units.h"

namespace geow2 {

class World;

// Entity flags (+0x24), named by what the code does with them.
enum EntityFlag : uint32_t {
  kEntityRemoved = 0x1,          // skipped by every walk
  kEntityPlayer = 0x2,
  kEntityTouches = 0x4,          // with 0x100 and without 0x40: hits players
  kEntityFadingIn = 0x10,        // +0x38 rises to 1, no update meanwhile
  kEntityNoTouch = 0x40,
  kEntityTouchable = 0x100,
  kEntityHasModel = 0x400,       // drawn by vtable slot 2
  kEntityPostMove = 0x800,       // vtable slot 4 after moving
  kEntityCounted = 0x10000,      // counted in dat_8219E828
  kEntityGlow = 0x1000000,       // vtable slot 1 (light) before drawing
};

// sub_82061DA0: one part of a model, each line coloured by line colour
// times `colour`, both ends through `m`, as line quads.
void draw_model_part(LineBatch& out, const ModelGeometry& g, size_t part, const Mat4& m, const float colour[4]);

// The game's entity base (sub_82045830 / sub_82047030 for unit-based
// ones): the common fields and the virtual slots the frame uses.
class Entity {
 public:
  virtual ~Entity() = default;

  // Slot 1 (sub_82045B18 for units): the unit's glow as a light sprite.
  virtual void light(World& w, std::vector<LightVertex>& out);
  // Slot 2 (sub_82046D88 for units): the model, white, or two growing
  // white ghosts while fading in.
  virtual void draw(World& w, LineBatch& out);
  // Slot 3: the type's update.
  virtual void update(World&) {}
  // Slot 4 (sub_82046BD8): keep the spatial hash cell current.
  virtual void post_move(World& w);
  // Slot 5: touched the arena edge; n points back in.
  virtual void wall(World&, Vec2) {}
  // Slot 6: the world matrix for style Custom.
  virtual Mat4 custom_placement(World& w);

  // sub_82045CF0: the world matrix by the unit's style.
  Mat4 placement(World& w);

  float x = 0, y = 0;          // +0x04
  float vx = 0, vy = 0;        // +0x0C
  float heading = 0;           // +0x14
  float sx = 1, sy = 1;        // +0x18
  float radius = 0;            // +0x20
  uint32_t flags = 0;          // +0x24
  float colour[4] = {1, 1, 1, 1};  // +0x28
  float fade = 0;              // +0x38
  int type = 0;                // +0x44
  const Entity* owner = nullptr;  // +0x48
  const UnitInfo* unit = nullptr;  // +0x78
  int cell = -1;               // +0x74
};

// A player as the entities see it (list 0x821813AC).
struct PlayerTarget {
  float x = 0, y = 0, vx = 0, vy = 0;
  float radius = 0;
  bool removed = false;    // flag 1
  float hidden = 0;        // +0xE0: targeted only while <= 0
};

// The playfield: entities in creation order (list 0x82181390), the 32 x 32
// spatial hash of 32-unit cells (dat_8219E850), the random generator and
// the players.
class World {
 public:
  World(const GameMath& gm, GameRng& rng, const Units& units) : gm(gm), rng(rng), units(units) {}

  Entity* add(std::unique_ptr<Entity> e);
  // One step of every entity (sub_820366A0 step 4, sub_820453E0 each).
  void update_entities();
  // The pre-render pass over the entities (sub_82065E38 / sub_82045740):
  // their light sprites (once per step, for the light map) and their
  // lines (for the picture).
  void light_entities(std::vector<LightVertex>& lights);
  void draw_entities(LineBatch& out);

  // sub_8204F458: the nearest player that can be targeted, or null.
  const PlayerTarget* nearest_player(const Entity& e) const;
  // sub_82045AB8.
  const PlayerTarget* target(const Entity& e) const;
  // sub_8203B8B0: a random vector, y drawn first.
  Vec2 random_vector(float scale);
  // sub_82030430.
  float random_float(float scale);
  // sub_8205B400: a random point in the arena, y drawn first.
  Vec2 random_position();
  // sub_82046248: push apart entities of the same type.
  void separate(Entity& e, float radius, float strength);
  // Hash cell of a position (sub_82046BD8): (int(y) & 0x3E0) | (int(x) >> 5 & 0x1F).
  static int cell_of(float x, float y);
  template <class F>
  void for_each_near(float x, float y, float r, F f);

  const GameMath& gm;
  GameRng& rng;
  const Units& units;
  float step = 1.0f / 60.0f;            // dat_8219D280
  float half_x = 300.0f, half_y = 200.0f;  // dat_821825B4 / dat_821825B8
  std::vector<PlayerTarget> players;
  std::vector<std::unique_ptr<Entity>> entities;
  std::array<std::vector<Entity*>, 1024> cells;
  int enemy_count = 0;                  // dat_8219E828
  bool frozen = false;                  // dat_8219E7BC
  int player_hits = 0;                  // touches reported this step (player slot 12)
};

// The cells overlapping (x - r .. x + r, y - r .. y + r), walked as
// sub_82046248 / sub_82055DA0 do (x fastest, both wrapping at 32).
template <class F>
void World::for_each_near(float x, float y, float r, F f) {
  const uint32_t x0 = uint32_t(int(double(x) - r) >> 5) & 0x1F, y0 = uint32_t(int(double(y) - r) >> 5) & 0x1F;
  const uint32_t x1 = uint32_t(int(double(x) + r) >> 5) & 0x1F, y1 = uint32_t(int(double(y) + r) >> 5) & 0x1F;
  uint32_t cx = (x0 - 1) & 0x1F, cy = y0;
  for (;;) {
    if (cx == x1) {
      if (cy == y1) return;
      cy = (cy + 1) & 0x1F;
      cx = x0;
    } else {
      cx = (cx + 1) & 0x1F;
    }
    for (Entity* e : cells[size_t(cy << 5 | cx)]) f(*e);
  }
}

}  // namespace geow2

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "game/effects.h"
#include "game/game_math.h"
#include "game/particles.h"
#include "game/light_map.h"
#include "game/line_quads.h"
#include "game/math.h"
#include "game/player_motion.h"
#include "game/rng.h"
#include "game/units.h"

namespace geow2 {

class World;

// Who killed an entity (the record passed to vtable slots 7 and 9): the
// shot, its owner (a player) and the kind of kill (1 a shot; 2 and 4 score
// nothing).
struct Killer {
  float x = 0, y = 0, vx = 0, vy = 0;  // the shot's position and velocity
  int kind = 0;
  int shot_bounces = 0;                // the shot's +0xA0
};

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
  // Slot 7 (sub_820459D8 for units): hit by a shot; kills it.
  virtual void hit_by_shot(World& w, const Killer& k) { kill(w, k); }
  // Slot 9 (sub_82046010(self, killer, 0) for units): destroyed.
  virtual void kill(World& w, const Killer& k);

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
  int shape_points = 0;        // +0x3C: points of the shape table at +0x40
  int type = 0;                // +0x44
  const Entity* owner = nullptr;  // +0x48
  const UnitInfo* unit = nullptr;  // +0x78
  // Built on the unit base (sub_82047030): in the units list (0x8218139C)
  // and the spatial hash. Blasts and ghosts are not.
  bool unit_based = true;
  int cell = -1;               // +0x74
};

// A player as the entities see it (list 0x821813AC).
struct PlayerTarget {
  float x = 0, y = 0, vx = 0, vy = 0;
  float radius = 0;
  bool removed = false;    // flag 1
  float hidden = 0;        // +0xE0: targeted only while <= 0
};

// The record a touch hands the player (vtable slot 12, sub_8204E8A0):
// the enemy as both killer and its owner, kind 2.
struct Touch {
  size_t player = 0;
  Entity* enemy = nullptr;
};

// The playfield: entities in creation order (list 0x82181390), the 32 x 32
// spatial hash of 32-unit cells (dat_8219E850), the random generator and
// the players.
class World {
 public:
  World(const GameMath& gm, GameRng& rng, const Units& units)
      : gm(gm), rng(rng), units(units), effects(gm, rng, particles) {}

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
  // sub_82035A40: flag for removal (deleted at the end of the step).
  void remove(Entity& e);
  // Delete the removed entities (the deferred list at dat_8219D278).
  void purge();
  // sub_820408D0: the score for killing `victim`.
  void award(const Entity& victim, const Killer& k);
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
  // A unit touching a player calls the player's slot 12 at once, inside
  // the walk (sub_820453E0): the game handles it here.
  std::function<void(const Touch&)> touch;
  int bomb_kills = 0;                   // dat_8219D284: shockwave kills this step
  // Kill markers (sub_82043418, type 0x19, list 0x82181420): they live one
  // step and push the grid (radius 32, strength 6).
  std::vector<Vec2> death_markers;
  // The player's score state (0x8219D068 + 0x78 * player): score (+0x28)
  // and multiplier (+0x40; the HUD shows x1 at the start).
  int64_t score = 0;
  int multiplier = 1;
  int kills = 0;                        // dat_8219D0A0
  // Particles and the effects that make them.
  Particles particles;
  ParticleTuning particle_tuning;
  Effects effects;
  std::vector<ParticleAttractor> attractors;
  std::vector<ParticleRepulsor> repulsors;
  bool king_mode = false;               // mode 5 doubles the score
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

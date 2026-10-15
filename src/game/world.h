#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "audio/events.h"
#include "game/debris.h"
#include "game/bullets.h"
#include "game/effects.h"
#include "game/game_math.h"
#include "game/particles.h"
#include "game/popups.h"
#include "game/light_map.h"
#include "game/line_quads.h"
#include "game/math.h"
#include "game/player_motion.h"
#include "game/rng.h"
#include "game/units.h"

namespace geow2 {

class World;
class Entity;

// Who killed an entity (the record passed to vtable slots 7 and 9): the
// shot, its owner (a player) and the kind of kill (1 a shot; 2 and 4 score
// nothing).
struct Killer {
  float x = 0, y = 0, vx = 0, vy = 0;  // the shot's position and velocity
  int kind = 0;
  int object_type = 0;                 // the killer object's type code (5: a player shot)
  const Entity* source = nullptr;      // the killer object itself, when the code needs it
  int blast_kind = -1;                 // a blast killer's kind (+0x68)
  int shot_bounces = 0;                // the shot's +0xA0
  // The player credited (the shot's or blast's owner, +0x48): an index
  // into World::players.
  size_t player = 0;
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

struct GeomShapes;

// Unit.Mote tuning variables (registration address, default): the geoms.
struct MoteTuning {
  // Unit.Mote.Colour (0x8219F308; dat_82001D9C, dat_82001DB4,
  // dat_82001DB8, dat_82001158).
  float colour[4] = {0.204710007f, 0.383774996f, 0.0780566037f, 2.5f};
  // Unit.Mote.GlowColour (0x8219F34C).
  float glow[4] = {0.2f, 0.8f, 0.1f, 6.0f};
  float glow_size = 25.0f;     // Unit.Mote.GlowSize (0x8219F390)
  float magnet_range = 45.0f;  // Unit.Mote.MagnetRange (0x8219F3C4)
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
  virtual void kill(World& w, const Killer& k) { die(w, k, 0); }
  // sub_82046010(self, killer, flags): unless flag 1, the killed effect,
  // line debris (in the entity's colour with flag 0x10, else white) and a
  // kill marker; unless 2, the score and geoms; unless 4, sound 2; unless
  // 8, removal.
  void die(World& w, const Killer& k, uint32_t f);
  // The points sub_820408D0 gives the types it scores by their own state
  // (rocket, gravity well, gate); 0 for the rest.
  virtual int special_points(const World&) const { return 0; }
  // The geoms sub_8204E108 drops for the types it counts by their own
  // state (gravity well, snake segment); 0 for the rest.
  virtual int special_geoms() const { return 0; }
  // The destructor's side effects (unlinking), when the object is deleted.
  virtual void on_delete(World&) {}

  // sub_82045CF0: the world matrix by the unit's style.
  Mat4 placement(World& w);
  // sub_82046D88 with a tint: the model's part 0 in `tint`, or while fading
  // in two growing ghosts in it with their alpha.
  void draw_unit(World& w, LineBatch& out, const float tint[4]);

  float x = 0, y = 0;          // +0x04
  float vx = 0, vy = 0;        // +0x0C (the wells' pulls and shoves add to it)
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
  uint64_t order = 0;          // its place in the object list (creation order)
};

// A player as the entities see it (list 0x821813AC).
struct PlayerTarget {
  float x = 0, y = 0, vx = 0, vy = 0;
  float prev_x = 0, prev_y = 0;  // +0xBC / +0xC0: position at the start of its update
  bool fading = false;             // flag 0x10
  float radius = 0;
  bool removed = false;    // flag 1
  float hidden = 0;        // +0xE0: targeted only while <= 0 (the freeze power-up)
  // +0xAC: when it spawned, plus a random fraction of a step (the earliest
  // spawned is the spawners' reference player, sub_8204F3F8).
  float spawn_time = 0;
  int record = 0;          // +0xA4: its score record
  int colour = 0;          // +0xA0: colour id (0 single player, 1..4 the team)
  bool magnet = false;     // +0xDC > 0: pulls geoms from four times as far
  // Its slot's gate chain (+0x34 count, +0x30 time; dat_8219CEE8 / CEE4).
  int gate_chain = 0;
  float gate_chain_time = 0.0f;
};

// A score record (0x8219D068 + 0x78 r): score (+0x28), multiplier (+0x40;
// the HUD shows x1 at the start), the next multiplier milestone (+0x44, 10
// from the record constructor 0x82034868), kills (+0x38, dat_8219D0A0)
// and King's zones touched (+0x64, dat_8219D0CC).
struct ScoreRecord {
  int64_t score = 0;
  int multiplier = 1;
  int milestone = 10;
  int kills = 0;
  int zones = 0;
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
  // A new game (sub_82036250 and the game block's reset): every object
  // gone, the counters and the player's score record reset. Particles,
  // debris and pop-ups carry on.
  void new_game();
  // sub_82035A70: every object in the object list deleted (the game's end
  // and start).
  void delete_objects();
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
  // sub_82035A40: flag for removal (deleted at the start of the next
  // step, by the game's step after its spawner).
  void remove(Entity& e);
  // The debris part of sub_82046010: the unit's model (part 0, through
  // `m`) shatters from where the killer `k` struck the circle of `radius`
  // around (x, y): 0.9 of the radius toward the killer, or the killer's
  // position when closer than 0.1.
  void shatter(const UnitInfo* unit, const Mat4& m, float x, float y, float radius, const Killer& k,
               const float colour[4]);
  // Delete the removed entities (the deferred list at dat_8219D278).
  void purge();
  // sub_820408D0: the score for killing `victim`.
  void award(const Entity& victim, const Killer& k);
  // Hash cell of a position (sub_82046BD8): (int(y) & 0x3E0) | (int(x) >> 5 & 0x1F).
  static int cell_of(float x, float y);
  template <class F>
  void for_each_near(float x, float y, float r, F f);
  // The same walk over cells x0..x1, y0..y1 (each wrapping at 32).
  template <class F>
  void for_each_cell(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, F f);

  const GameMath& gm;
  GameRng& rng;
  const Units& units;
  float step = 1.0f / 60.0f;            // dat_8219D280
  float half_x = 300.0f, half_y = 200.0f;  // dat_821825B4 / dat_821825B8
  std::vector<PlayerTarget> players;
  // The player list (0x821813A8): the players' objects in creation order,
  // as indices into `players`.
  std::vector<size_t> player_order;
  std::vector<std::unique_ptr<Entity>> entities;
  std::array<std::vector<Entity*>, 1024> cells;
  int enemy_count = 0;                  // dat_8219E828
  bool frozen = false;                  // dat_8219E7BC
  // A unit touching a player calls the player's slot 12 at once, inside
  // the walk (sub_820453E0): the game handles it here.
  std::function<void(const Touch&)> touch;
  int bomb_kills = 0;                   // dat_8219D284: shockwave kills this step
  float clock = 0.0f;                   // dat_8219CE68: the game time
  // The spawners' feedback: kills by weight (DB50, sub_8203BAC8) and geom
  // value collected (DB54, sub_8203BBB8).
  int spawn_kills = 0, spawn_geoms = 0;
  // sub_82040818 per record: whether its kills score now (Deadline: before
  // time is up; the lives modes: while one of its ships is in the game or
  // can come back).
  std::array<bool, 4> scoring_open{true, true, true, true};
  // The rest of sub_820408D0 after a record's score rises (extra lives and
  // bombs by score): the game handles it here.
  std::function<void(int record, size_t player)> scored;
  // sub_820787F8: a sound event for the audio engine.
  std::function<void(audio::Event)> sound;
  void play(audio::Event e) const {
    if (sound) sound(e);
  }
  // sub_820390F0: a geom's value picked up (Sequence's level statistics).
  std::function<void(int)> geom_collected;
  // The repulsar's deflection sound has its own 35 ms gate (dat_8219FACC).
  std::function<void()> repulsar_hit;
  // sub_8203BEE8's first-spawn latches (dat_8219DB74, cleared at each game
  // start): NewEnemyType on a type's first spawn.
  std::array<bool, 0x1A> spawned_type{};
  // sub_82078610: a gravity well reports its hits this frame (the hum).
  std::function<void(const Entity*, int)> well_hum;
  // sub_82034CA0(slot 0, bit, flags): an achievement earned.
  std::function<void(uint32_t bit, int flags)> achieve;
  // Deadline's kills by shots that bounced off a gate (dat_8219CEF0) and
  // the HUD hint's timer (dat_8219CEF4).
  int bounce_kills = 0;
  float bounce_hint = 0.0f;
  // The C runtime's rand() (Evolved's spawner).
  CrtRand crt;
  // Kill markers (sub_82043418, type 0x19, list 0x82181420): they live one
  // step and push the grid (radius 32, strength 6).
  std::vector<Vec2> death_markers;
  // The score records (one in single player and co-op, one per team in
  // versus).
  std::array<ScoreRecord, 4> records{};
  ScoreRecord& record_of(size_t player) { return records[size_t(players[player].record)]; }
  const ScoreRecord& record_of(size_t player) const { return records[size_t(players[player].record)]; }
  // The player a kill being scored is credited to (the special points of
  // gates read its chain).
  size_t scorer = 0;
  bool multiplayer = false;             // dat_8219D275
  Popups popups;
  std::string bonus_multiplier_text = "Bonus multiplier";  // ID_X_BONUS_MULTIPLIER (0x79)
  // Particles and the effects that make them.
  Particles particles;
  ParticleTuning particle_tuning;
  Effects effects;
  std::vector<ParticleAttractor> attractors;
  std::vector<ParticleRepulsor> repulsors;
  // The player shots (their hash dat_8219F9AC is read by weavers).
  std::vector<Bullet>* shots = nullptr;
  // Creation order of everything in the object list (0x82181390): the
  // entities and the shots, walked together.
  uint64_t next_order = 1;
  void enlist(Bullet& b) { b.order = next_order++; }
  // Line debris (Render.Layer.Smash).
  Debris debris;
  SmashTuning smash;
  bool king_mode = false;               // mode 5 doubles the score
  int mode = 0;                         // dat_8219D254 (0 Deadline)
  // Geoms: their shapes (from the user's image; none, no geoms) and tuning.
  const GeomShapes* geom_shapes = nullptr;
  MoteTuning motes;
};

// The cells overlapping (x - r .. x + r, y - r .. y + r), walked as
// sub_82046248 / sub_82055DA0 do (x fastest, both wrapping at 32).
template <class F>
void World::for_each_near(float x, float y, float r, F f) {
  const uint32_t x0 = uint32_t(int(double(x) - r) >> 5) & 0x1F, y0 = uint32_t(int(double(y) - r) >> 5) & 0x1F;
  const uint32_t x1 = uint32_t(int(double(x) + r) >> 5) & 0x1F, y1 = uint32_t(int(double(y) + r) >> 5) & 0x1F;
  for_each_cell(x0, y0, x1, y1, f);
}

template <class F>
void World::for_each_cell(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, F f) {
  uint32_t cx = (x0 - 1) & 0x1F, cy = y0;
  for (;;) {
    if (cx == x1) {
      if (cy == y1) return;
      cy = (cy + 1) & 0x1F;
      cx = x0;
    } else {
      cx = (cx + 1) & 0x1F;
    }
    // By index: what `f` adds to the cell is visited too, as in the
    // original's linked lists.
    auto& cell = cells[size_t(cy << 5 | cx)];
    for (size_t i = 0; i < cell.size(); ++i) f(*cell[i]);
  }
}

}  // namespace geow2

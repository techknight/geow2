// geow2: the game.
//
//   geow2 [--data <cache-dir>]
//   geow2 --data <cache-dir> --frames <n> --screenshot <out.png> [--size <w> <h>] [--stick <x> <y>]
//         [--fire <x> <y>] [--bomb <step>]
//
// The second form runs n simulation steps without a window (input: a fixed
// raw stick value, XInput convention; --bomb pulls the trigger on one
// step) and saves the final frame.
//
// Early development build: Deadline's arena with the player (shots,
// shield, bombs, death and respawn) and its wanderers, drawn as the
// original's glowing line quads with its bloom.

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "app/png.h"
#include "app/renderer.h"
#include "data/flask.h"
#include "extract/bytes.h"
#include "game/bullets.h"
#include "game/camera.h"
#include "game/deadline.h"
#include "game/game_math.h"
#include "game/geoms.h"
#include "game/grid.h"
#include "game/killer_ghost.h"
#include "game/light_map.h"
#include "game/line_quads.h"
#include "game/model_geometry.h"
#include "game/player.h"
#include "game/player_motion.h"
#include "game/rng.h"
#include "game/shockwave.h"
#include "game/stars.h"
#include "game/units.h"
#include "game/world.h"
#include "game/rotation.h"
#include "game/shots.h"

namespace fs = std::filesystem;
using namespace geow2;

namespace {

constexpr float kStep = 1.0f / 60.0f;          // dat_8219D280 = 1.0 / 60.0 (sub_8205D538)
constexpr float kArenaHalfX = 300.0f;          // dat_82000CF4 (sub_82036250)
constexpr float kArenaHalfY = 200.0f;          // dat_82000CF0
constexpr float kShapeCapSize = 1.0f;          // dat_820006C4 (sub_82061908)

const FlaskStruct* as_struct(const FlaskValue* v) {
  auto* p = v ? std::get_if<std::shared_ptr<FlaskStruct>>(&v->v) : nullptr;
  return p ? p->get() : nullptr;
}

float as_float(const FlaskValue* v) {
  auto* d = v ? std::get_if<double>(&v->v) : nullptr;
  return d ? float(*d) : 0.0f;
}

// The Unit record with this name (the original looks units up by name,
// sub_82031D40), its decoded value kept alive with the struct.
struct Unit {
  FlaskValue value;
  const FlaskStruct* s = nullptr;
};

std::optional<Unit> find_unit(const Flask& db, const std::string& name) {
  for (uint32_t r = 0; r < db.records().size(); ++r) {
    if (db.types()[db.records()[r].type].name != "Unit") continue;
    Unit u{db.decode(r)};
    u.s = as_struct(&u.value);
    const FlaskValue* n = u.s ? u.s->find("name") : nullptr;
    if (n && std::holds_alternative<std::string>(n->v) && std::get<std::string>(n->v) == name) return u;
  }
  return std::nullopt;
}

struct Game {
  std::unique_ptr<Flask> db;
  std::unique_ptr<GameMath> gm;
  ModelGeometry ship;
  float tint[4] = {1, 1, 1, 1};  // player colour (constructor: unit tint, rgb times alpha)
  PlayerMotion player;
  CameraState camera;
  PlayerWeapon weapon;
  std::vector<Bullet> bullets;
  BulletShape bullet_shape;
  GeomShapes geom_shapes;
  // The player's light: unit glowColour (rgb times alpha) and glowSize
  // (sub_82050D00).
  float glow[3] = {0, 0, 0};
  float glow_size = 0.0f;
  // The background grid and what each step produces for drawing.
  Grid grid;
  GridTuning grid_tuning;
  float grid_fade = 0.0f;                // dat_821A170C
  bool grid_visible = false;
  std::vector<GridVertex> grid_lines;    // this step's line quads
  std::vector<GridVertex> warp_mesh;     // this step's light map warp
  std::vector<LightVertex> lights;       // this step's light sprites
  GameRng rng;
  StarField stars;
  std::vector<StarVertex> star_vertices;  // this step's stars
  std::vector<GridVertex> walls;          // the arena walls (fixed)
  std::vector<LineVertex> particle_quads;  // this step's particle streaks
  // Enemies: the units of Data.bin, the playfield and Deadline's spawner.
  std::unique_ptr<Units> units;
  std::unique_ptr<World> world;
  DeadlineSpawner spawner;
  float elapsed = 0.0f;  // dat_8219CE68
  // The player's life and the mode's rules (Deadline, sub_82034EA0).
  PlayerLife life;
  ModeRules rules;
  const UnitInfo* player_unit = nullptr;
  bool field_live = false;   // dat_8219CE7D: a player spawned since the last clear
  bool was_fading = false;   // dat_8219D3D0: a player was fading in last step
};

// The player's death (vtable slot 9, sub_8204E988), with the enemy that
// touched it as the killer.
void kill_player(Game& g, Entity& killer) {
  World& w = *g.world;
  PlayerLife& p = g.life;
  // The slot keeps where the player died (the next spawn point in single
  // player) and the mode's delay; sound event 4.
  p.spawn_x = g.player.x;
  p.spawn_y = g.player.y;
  p.respawn_wait = g.rules.respawn_delay;
  ++p.deaths;
  // The killer gives way to its ghost (sub_82044FA8), then
  // sub_82046010(player, killer, 0x1E): the player's killed effect, line
  // debris (not ported yet) and a kill marker; no score, no sound 2.
  replace_killer(w, killer);
  if (g.player_unit) w.effects.trigger(g.player_unit->killed, g.player.x, g.player.y);
  w.death_markers.push_back({g.player.x, g.player.y});
  // (In Deadline, dat_8219CEF0 between 11 and 74 sets dat_8219CEF4 to 4:
  // with the HUD.)
  p.removed = true;
  w.players[0].removed = true;
  // (dat_8219D288 = 1 when no player is left: with the game-over flow.)
}

// A unit touching the player: slot 12 (sub_8204E8A0) passes it to slot 7
// (sub_8204EEA8) as a kind 2 killer.
void player_touched(Game& g, const Touch& t) {
  if (g.rules.mode == 7 && (t.enemy->type == 0x13 || t.enemy->type == 3)) return;
  World& w = *g.world;
  PlayerLife& p = g.life;
  // dat_8219F500 (never set here) and the bomb wait guard the player.
  if (!(p.bomb_wait <= 0.0f)) return;
  if (p.shield <= 0.0f) {
    kill_player(g, *t.enemy);
    if (g.rules.mode == 0) g.spawner.player_died(g.rules.length - g.elapsed);
    return;
  }
  // The shield destroys what touches it (sound event 9): a kind 2 kill by
  // the player for touchable units (no score), removal for flag 8.
  Entity& e = *t.enemy;
  if (e.flags & kEntityTouchable) {
    Killer k;
    k.x = g.player.x;
    k.y = g.player.y;
    k.vx = g.player.vx;
    k.vy = g.player.vy;
    k.kind = 2;
    e.kill(w, k);
    ++p.shield_kills;
  } else if (e.flags & 0x8) {
    w.remove(e);
  }
}

// A new player object (sub_82051448) at the slot's spawn point: radius 5
// (dat_820006F0), heading -pi/2 (dat_820010F4), at rest, fading in.
void spawn_player(Game& g) {
  g.player = PlayerMotion{};
  g.player.x = g.life.spawn_x;
  g.player.y = g.life.spawn_y;
  g.player.radius = 5.0f;
  g.player.heading = -1.57079637f;
  g.weapon = PlayerWeapon{};
  player_spawned(g.life);
  PlayerTarget& t = g.world->players[0];
  t = PlayerTarget{};
  t.x = g.player.x;
  t.y = g.player.y;
  t.radius = g.player.radius;
}

// sub_82036358, when no player is left after one spawned (sound event
// 0x50): every unit but the players is destroyed (kind 0: no score or
// geoms) and the shots, geoms and blasts go. (List 0x821813D0 comes with
// its objects.)
void clear_field(Game& g) {
  World& w = *g.world;
  for (size_t i = 0; i < w.entities.size(); ++i) {
    Entity& e = *w.entities[i];
    if (!e.unit_based || (e.flags & 3)) continue;
    Killer k;
    k.x = e.x;
    k.y = e.y;
    k.vx = e.vx;
    k.vy = e.vy;
    k.kind = 0;
    e.kill(w, k);
  }
  g.bullets.clear();
  for (auto& e : w.entities)
    if ((e->type == 0x22 || e->type == 0xF) && !(e->flags & kEntityRemoved)) w.remove(*e);
}

// The spawn block of sub_820366A0 for the one slot.
void respawn(Game& g) {
  PlayerLife& p = g.life;
  // With no player in the game and none able to come back within a
  // second, every slot waits a second.
  if (!p.alive()) {
    const bool can = g.rules.lives_mode != 0 || p.free_spawn || p.lives > 0;
    if (!(can && p.respawn_wait < 1.0f)) p.respawn_wait = 1.0f;
  }
  if (p.present) return;
  const float left = p.respawn_wait - kStep;
  p.respawn_wait = left < 0.0f ? 0.0f : left;
  if (!(p.respawn_wait <= 0.0f)) return;
  bool spawn = true;
  if (g.rules.lives_mode == 0) {
    if (p.free_spawn)
      p.free_spawn = false;
    else if (p.lives > 0)
      --p.lives;
    else
      spawn = false;
  }
  if (!spawn) return;
  spawn_player(g);  // sound event 3
  g.field_live = true;
}

// A player starting to fade in, single player (sub_820366A0): 40 rounds of
// 64 blue particles (speed up to 25, depth times 0.9) and 8 red ones
// (speed up to 5) from its position.
void spawn_burst(Game& g) {
  World& w = *g.world;
  const float x = g.player.x, y = g.player.y;
  const float blue[3] = {2.0f, 2.0f, 12.0f};  // dat_820006FC, dat_82000D68
  const float red[3] = {12.0f, 2.0f, 2.0f};
  for (int i = 0; i < 40; ++i) {
    for (int j = 0; j < 64; ++j) {
      float v[4];
      w.effects.random_in_ball(25.0f, v);  // dat_82000D6C
      w.particles.add(x, y, 0.0f, v[0], v[1], v[2] * 0.9f, blue);  // dat_82000D70
    }
    for (int j = 0; j < 8; ++j) {
      float v[4];
      w.effects.random_in_ball(5.0f, v);  // dat_820006F0
      w.particles.add(x, y, 0.0f, v[0], v[1], v[2], red);
    }
  }
}

bool load(Game& g, const fs::path& cache, int64_t seed) {
  try {
    g.db = std::make_unique<Flask>(read_file(cache / "files" / "Data.bin"));
    g.gm = std::make_unique<GameMath>(GameMath::from_image(read_file(cache / "xex" / "image.bin")));
  } catch (const std::exception& e) {
    SDL_Log("cannot load the cache at %s: %s (run geow2-extract unpack first)", cache.string().c_str(), e.what());
    return false;
  }
  // The player constructor (sub_82051448) starts from the unit "player"
  // (dat_82000F18); a colour index above 0 (its seventh argument, +0xA0)
  // switches the model to player1..4 by the eighth and the tint to
  // player<index>. Single player is taken to use index 0: the original's
  // ship is white there, as "player"'s tint is (oracle).
  const auto unit = find_unit(*g.db, "player");
  if (!unit) {
    SDL_Log("Data.bin has no player unit");
    return false;
  }
  if (const FlaskStruct* tint = as_struct(unit->s->find("tint"))) {
    const float a = as_float(tint->find("a"));
    g.tint[0] = as_float(tint->find("r")) * a;
    g.tint[1] = as_float(tint->find("g")) * a;
    g.tint[2] = as_float(tint->find("b")) * a;
    g.tint[3] = 1.0f;
  }
  if (const FlaskStruct* glow = as_struct(unit->s->find("glowColour"))) {
    const float a = as_float(glow->find("a"));
    g.glow[0] = a * as_float(glow->find("r"));
    g.glow[1] = as_float(glow->find("g")) * a;
    g.glow[2] = as_float(glow->find("b")) * a;
  }
  g.glow_size = as_float(unit->s->find("glowSize"));
  // The grid at rest over the arena (sub_8205B000 from sub_82036250).
  g.grid.reset(kArenaHalfX, kArenaHalfY);
  // The generator is seeded from the tick count at start-up
  // (sub_82037C20), then the starfield draws from it (sub_82075318).
  if (seed >= 0)
    g.rng.seed(uint32_t(seed));  // --seed, for reproducible captures
  else
    g.rng.seed_from_ticks(SDL_GetTicksNS() / 1000000);
  g.stars.init(g.rng);
  g.units = std::make_unique<Units>(Units::load(*g.db, *g.gm));
  g.world = std::make_unique<World>(*g.gm, g.rng, *g.units);
  g.world->players.resize(1);
  g.world->players[0].radius = 5.0f;
  g.world->touch = [&g](const Touch& t) { player_touched(g, t); };
  wall_quads(kArenaHalfX, kArenaHalfY, g.grid_tuning.depth, kWallOuterScale, g.walls);
  // No player until the first spawn: the slot record starts zeroed (no
  // wait, spawn point at the centre), so it spawns at the end of the first
  // step. Lives and bombs come from the mode (sub_82032FF8).
  g.player_unit = g.units->find("player");
  g.life.lives = g.rules.start_lives;
  g.life.bombs = g.rules.start_bombs;
  g.world->players[0].removed = true;
  const FlaskValue* model_ref = unit->s->find("model");
  const auto* ref = model_ref ? std::get_if<FlaskRef>(&model_ref->v) : nullptr;
  if (!ref) {
    SDL_Log("player has no model");
    return false;
  }
  const FlaskValue model = g.db->decode(ref->record);
  if (const FlaskStruct* m = as_struct(&model)) g.ship = build_model_geometry(*m, *g.gm);
  try {
    const auto image = read_file(cache / "xex" / "image.bin");
    g.bullet_shape = BulletShape::from_image(image);
    g.geom_shapes = GeomShapes::from_image(image);
  } catch (const std::exception& e) {
    SDL_Log("cannot read the shot and geom shapes: %s", e.what());
    return false;
  }
  g.world->geom_shapes = &g.geom_shapes;
  g.world->mode = g.rules.mode;
  return true;
}

// One simulation step with a raw (XInput convention) left stick value.
struct Sticks {
  int16_t move_x = 0, move_y = 0, fire_x = 0, fire_y = 0;  // raw, XInput convention
  uint8_t left_trigger = 0, right_trigger = 0;               // raw
};

// The player list's walk (sub_820453E0 for the player): fading in first;
// once in, the update (sub_8204FF00: thrust, firing, the bomb, the timers)
// and the move; then the arena edge.
void update_player(Game& g, const Sticks& s) {
  World& w = *g.world;
  PlayerLife& p = g.life;
  if (p.fading) {
    p.fade = std::fmaf(kStep, 1.5f, p.fade);  // dat_82000C90
    if (p.fade >= 1.0f) {
      p.fade = 1.0f;
      p.fading = false;
    }
  }
  if (!p.fading) {
    const Vec2 move = stick_vector(stick_axis(s.move_x, false), stick_axis(s.move_y, true));
    const Vec2 fire = stick_vector(stick_axis(s.fire_x, false), stick_axis(s.fire_y, true));
    player_thrust(g.player, move, *g.gm);
    player_fire_step(g.player, g.weapon, fire, kStep, *g.gm, g.bullets);
    // The larger trigger (axes 8 and 9) sets off a bomb: a blast from the
    // player's position, and the spawner backs off (sub_8203E4B0).
    const float lt = trigger_axis(s.left_trigger), rt = trigger_axis(s.right_trigger);
    if (player_bomb(p, lt - rt < 0.0f ? rt : lt, g.rules)) {
      w.add(std::make_unique<Shockwave>(w, 400.0f, 533.2f, g.player.x, g.player.y, 0));  // dat_82000F88, dat_82001C68
      g.spawner.bombed();
    }
    player_timers(p, kStep);
    integrate(g.player, kStep);
  }
  Vec2 n;
  if (arena_contact(g.player.radius, kArenaHalfX, kArenaHalfY, g.player.x, g.player.y, n)) player_wall(g.player, n);
}

void step(Game& g, const Sticks& s) {
  World& w = *g.world;
  w.bomb_kills = 0;
  // Deadline's spawner (sub_8203E7E8) runs while a player is in the game;
  // then the clock (sub_820366A0).
  if (g.life.alive()) g.spawner.step(w, g.elapsed);
  g.elapsed = kStep + g.elapsed;
  // Last step's kill markers go (sub_82043368).
  w.death_markers.clear();

  if (g.life.alive()) update_player(g, s);
  PlayerTarget& target = w.players[0];
  target.x = g.player.x;
  target.y = g.player.y;
  target.vx = g.player.vx;
  target.vy = g.player.vy;
  // Then the other entities, bullets included (the player list is updated
  // first, so bullets fired this step already move: to verify in the oracle).
  std::erase_if(g.bullets, [](Bullet& b) { return !bullet_step(b, kStep, kArenaHalfX, kArenaHalfY); });
  w.update_entities();
  // Shots against enemies (sub_820523B0).
  shot_collisions(w, g.bullets);
  std::erase_if(g.bullets, [](const Bullet& b) { return b.removed; });
  // Geoms are pulled in and picked up (sub_8204D410).
  collect_geoms(w);
  // A player that has just started to fade in bursts into view.
  const bool fading = g.life.alive() && g.life.fading;
  if (fading && !g.was_fading) spawn_burst(g);
  g.was_fading = fading;
  // The camera follows after the entity updates (sub_820366A0): the
  // player object while it exists, else the slot's spawn point.
  if (g.life.present)
    camera_follow_single(g.camera, &g.player.x, &g.player.y, 1, kArenaHalfX, kArenaHalfY);
  else
    camera_follow_single(g.camera, &g.life.spawn_x, &g.life.spawn_y, 1, kArenaHalfX, kArenaHalfY);
  // No player left since one spawned: the field is cleared; then a slot
  // without a player may spawn one.
  if (g.field_live && !g.life.alive()) {
    clear_field(g);
    g.field_live = false;
  }
  respawn(g);

  // The grid (sub_8205C8F8, then sub_8205B880 on worker threads): pushes
  // gathered from the entities, the spring step, and the line pass from
  // the positions the step read. Bullets push with radius +0x8C and
  // strength 2.0 (sub_8205B578).
  GridInputs in;
  // The specials: a fading-in player pulls, the first blast pushes.
  if (g.life.alive() && g.life.fading) in.fading_player = Vec2{g.player.x, g.player.y};
  for (const auto& e : w.entities)
    if (e->type == 0xF) {
      in.special = Vec2{e->x, e->y};
      break;
    }
  in.pushes.emplace_back();
  for (const Bullet& b : g.bullets) in.pushes.back().push_back({b.x, b.y, b.grid_radius, 2.0f});
  // Kill markers: radius 32 (dat_82001140), strength 6 (dat_82001414).
  in.pushes.emplace_back();
  for (const Vec2& m : w.death_markers) in.pushes.back().push_back({m.x, m.y, 32.0f, 6.0f});
  g.grid.step(in, kArenaHalfX, kArenaHalfY);
  grid_lines(g.grid, g.grid_tuning, g.grid_fade, g.grid_lines);
  grid_warp_mesh(g.grid, g.grid_tuning, g.warp_mesh);
  // The draw then eases the fade in (sub_8205CAF0; in game, so toward 1).
  g.grid_fade = grid_fade_step(g.grid_fade, true);
  g.grid_visible = g.grid_fade > kGridVisible;

  // Light sprites (sub_82065E38 walks the lists; bullets come before the
  // player): bullets size radius * Unit.Projectile.LightFactor (6.0),
  // colour * 8 (sub_82051DB8); the player its glow (sub_82050D00).
  g.lights.clear();
  w.light_entities(g.lights);
  for (const Bullet& b : g.bullets) {
    const float c[3] = {b.colour[0] * 8.0f, b.colour[1] * 8.0f, b.colour[2] * 8.0f};
    emit_light(g.lights, b.radius * 6.0f, b.x, b.y, c);
  }
  if (g.life.alive()) emit_light(g.lights, g.glow_size, g.player.x, g.player.y, g.glow);

  // The colour wheel turns (sub_82030A98); the particles step on the
  // render workers (sub_82063A38), after the entities' lights.
  w.effects.step(kStep);
  g.particle_quads.clear();
  w.particles.step(w.particle_tuning, kStep, w.half_x, w.half_y, w.attractors, w.repulsors, g.particle_quads,
                   g.lights);

  // The stars' step (sub_82075548, on the render worker each frame).
  g.stars.update(*g.gm, 1.0f, g.star_vertices);

  // Entities removed this step are deleted, the player object with them.
  w.purge();
  if (g.life.removed) g.life.present = false;
}

// The player's draw (vtable slot 2, sub_82050F30): the body (part 0) and,
// while +0x90 is set, the shield (part 1), in the player's colour. Fading
// in, two growing ghosts as the units' (alpha folded, size unfolded).
void draw_player(const Game& g, LineBatch& out) {
  if (!g.life.alive()) return;
  const Mat4 m = placement_style_none(*g.gm, g.player.x, g.player.y, g.player.heading);
  if (!g.life.fading) {
    draw_model_part(out, g.ship, 0, m, g.tint);
    if (g.life.shield_shown) draw_model_part(out, g.ship, 1, m, g.tint);
    return;
  }
  for (int i = 0; i < 2; ++i) {
    const float t = float(std::fmod(double(g.life.fade * 3.0f), 1.0)) + float(i);  // sub_8212FC00
    float f = t;
    if (f >= 1.0f) f = float(2.0 - double(f));
    if (f < 0.025f) continue;  // dat_82000D64
    const float s = std::fmaf(t, 1.4f, 0.1f);  // dat_82000FF8, dat_82000714
    Mat4 scaled = m;
    for (int r = 0; r < 3; ++r)
      for (int c = 0; c < 4; ++c) scaled.m[r * 4 + c] = m.m[r * 4 + c] * s;
    const float colour[4] = {g.tint[0], g.tint[1], g.tint[2], g.tint[3] * f};
    draw_model_part(out, g.ship, 0, scaled, colour);
    if (g.life.shield_shown) draw_model_part(out, g.ship, 1, scaled, colour);
  }
}

// The foreground lines: the enemies' models, the shots' shapes
// (sub_82061908) in their colour and the player, each line coloured as
// sub_82061DA0 does (line colour times the draw colour) with the model's
// cap size.
void foreground(const Game& g, LineBatch& out) {
  out.clear();
  // The pre-render pass's list order: enemies, shots, then the player.
  g.world->draw_entities(out);
  // The glow sprite of the shots is not drawn yet.
  for (const Bullet& b : g.bullets)
    for (const Segment& s : bullet_segments(b, g.bullet_shape, *g.gm))
      emit_line(out, kLineWidthUnit, kShapeCapSize, s.a, s.b, b.colour);
  draw_player(g, out);
}

void render(Renderer& r, SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* target, SDL_GPUTextureFormat format,
            uint32_t w, uint32_t h, const Game& g) {
  static LineBatch front;
  foreground(g, front);
  r.draw(cmd, target, format, w, h, camera_view_projection(g.camera, *g.gm), g.star_vertices,
         g.grid_visible ? &g.grid_lines : nullptr, g.walls, g.particle_quads, front);
}

// One simulation step and its light map update (one per step, as the
// original renders one frame per step).
void tick(Game& g, const Sticks& s, Renderer& r, SDL_GPUCommandBuffer* cmd) {
  step(g, s);
  r.update_light_map(cmd, g.warp_mesh, g.lights);
}

int run_headless(SDL_GPUDevice* dev, Renderer& r, Game& g, int frames, const Sticks& sticks, int bomb_step,
                 const fs::path& out, uint32_t w, uint32_t h) {
  for (int i = 0; i < frames; ++i) {
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev);
    Sticks s = sticks;
    if (i == bomb_step) s.right_trigger = 255;
    tick(g, s, r, cmd);
    SDL_SubmitGPUCommandBuffer(cmd);
  }
  SDL_GPUTextureCreateInfo tci{};
  tci.type = SDL_GPU_TEXTURETYPE_2D;
  tci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
  tci.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
  tci.width = w;
  tci.height = h;
  tci.layer_count_or_depth = 1;
  tci.num_levels = 1;
  SDL_GPUTexture* tex = SDL_CreateGPUTexture(dev, &tci);
  SDL_GPUTransferBufferCreateInfo dl{SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, w * h * 4, 0};
  SDL_GPUTransferBuffer* down = SDL_CreateGPUTransferBuffer(dev, &dl);
  if (!tex || !down) {
    SDL_Log("cannot create the offscreen target: %s", SDL_GetError());
    return 1;
  }
  SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev);
  render(r, cmd, tex, tci.format, w, h, g);
  SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
  SDL_GPUTextureRegion src{};
  src.texture = tex;
  src.w = w;
  src.h = h;
  src.d = 1;
  SDL_GPUTextureTransferInfo dst{down, 0, w, h};
  SDL_DownloadFromGPUTexture(copy, &src, &dst);
  SDL_EndGPUCopyPass(copy);
  SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
  SDL_WaitForGPUFences(dev, true, &fence, 1);
  SDL_ReleaseGPUFence(dev, fence);
  const auto* px = static_cast<const uint8_t*>(SDL_MapGPUTransferBuffer(dev, down, false));
  write_png(out, w, h, {px, size_t(w) * h * 4});
  SDL_UnmapGPUTransferBuffer(dev, down);
  SDL_Log("wrote %s after %d steps: player at (%.3f, %.3f), heading %.4f, camera (%.3f, %.3f) zoom %.3f, %zu bullets, "
          "%zu entities, score %lld x%d, %zu particles, %d deaths, %d bombs left, %d bomb kills",
          out.string().c_str(), frames, g.player.x, g.player.y, g.player.heading, g.camera.x, g.camera.y,
          g.camera.zoom, g.bullets.size(), g.world->entities.size(), (long long)g.world->score, g.world->multiplier, g.world->particles.size(),
          g.life.deaths, g.life.bombs, g.world->bomb_kills);
  SDL_ReleaseGPUTransferBuffer(dev, down);
  SDL_ReleaseGPUTexture(dev, tex);
  return 0;
}

int16_t clamp16(int v) { return int16_t(std::clamp(v, -32768, 32767)); }

int run_window(SDL_GPUDevice* dev, Renderer& r, Game& g) {
  SDL_Window* window = SDL_CreateWindow("geow2", 1280, 720, SDL_WINDOW_RESIZABLE);
  if (!window || !SDL_ClaimWindowForGPUDevice(dev, window)) {
    SDL_Log("cannot open the window: %s", SDL_GetError());
    return 1;
  }
  const SDL_GPUTextureFormat format = SDL_GetGPUSwapchainTextureFormat(dev, window);
  SDL_Gamepad* pad = nullptr;
  uint64_t last = SDL_GetTicksNS();
  double pending = 0.0;
  for (bool running = true; running;) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_EVENT_QUIT) running = false;
      if (e.type == SDL_EVENT_GAMEPAD_ADDED && !pad) pad = SDL_OpenGamepad(e.gdevice.which);
      if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) running = false;
    }
    // Sticks in the XInput convention (SDL's Y axes point down): left
    // moves, right fires (the original's default, as its rules screens show).
    int mx = 0, my = 0, fx = 0, fy = 0, lt = 0, rt = 0;
    if (pad) {
      mx = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX);
      my = -int(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY));
      fx = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTX);
      fy = -int(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY));
      // SDL's triggers run 0..32767; XInput's are bytes.
      lt = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) * 255 / 32767;
      rt = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) * 255 / 32767;
    }
    const bool* keys = SDL_GetKeyboardState(nullptr);
    if (keys[SDL_SCANCODE_A]) mx = -32768;
    if (keys[SDL_SCANCODE_D]) mx = 32767;
    if (keys[SDL_SCANCODE_W]) my = 32767;
    if (keys[SDL_SCANCODE_S]) my = -32768;
    if (keys[SDL_SCANCODE_LEFT]) fx = -32768;
    if (keys[SDL_SCANCODE_RIGHT]) fx = 32767;
    if (keys[SDL_SCANCODE_UP]) fy = 32767;
    if (keys[SDL_SCANCODE_DOWN]) fy = -32768;
    if (keys[SDL_SCANCODE_SPACE]) rt = 255;
    const Sticks sticks{clamp16(mx), clamp16(my), clamp16(fx), clamp16(fy), uint8_t(std::clamp(lt, 0, 255)),
                        uint8_t(std::clamp(rt, 0, 255))};

    // Fixed 1/60 s steps from real time; the picture shows the latest step.
    const uint64_t now = SDL_GetTicksNS();
    pending += double(now - last) / 1e9;
    last = now;
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev);
    for (int n = 0; pending >= kStep && n < 8; ++n) {
      tick(g, sticks, r, cmd);
      pending -= kStep;
    }
    if (pending > kStep) pending = 0.0;

    SDL_GPUTexture* swap = nullptr;
    uint32_t w = 0, h = 0;
    if (SDL_WaitAndAcquireGPUSwapchainTexture(cmd, window, &swap, &w, &h) && swap) render(r, cmd, swap, format, w, h, g);
    SDL_SubmitGPUCommandBuffer(cmd);
  }
  SDL_ReleaseWindowFromGPUDevice(dev, window);
  SDL_DestroyWindow(window);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  fs::path cache = fs::path(SDL_GetBasePath() ? SDL_GetBasePath() : "") / "cache";
  int frames = -1;
  Sticks sticks;
  fs::path screenshot;
  uint32_t shot_w = 1280, shot_h = 720;
  int64_t seed = -1;
  int bomb_step = -1;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--data" && i + 1 < argc) cache = argv[++i];
    else if (a == "--frames" && i + 1 < argc) frames = std::atoi(argv[++i]);
    else if (a == "--screenshot" && i + 1 < argc) screenshot = argv[++i];
    else if (a == "--seed" && i + 1 < argc) seed = std::atoll(argv[++i]);
    else if (a == "--size" && i + 2 < argc) {
      shot_w = uint32_t(std::atoi(argv[++i]));
      shot_h = uint32_t(std::atoi(argv[++i]));
    } else if (a == "--stick" && i + 2 < argc) {
      sticks.move_x = clamp16(std::atoi(argv[++i]));
      sticks.move_y = clamp16(std::atoi(argv[++i]));
    } else if (a == "--bomb" && i + 1 < argc) {
      bomb_step = std::atoi(argv[++i]);
    } else if (a == "--fire" && i + 2 < argc) {
      sticks.fire_x = clamp16(std::atoi(argv[++i]));
      sticks.fire_y = clamp16(std::atoi(argv[++i]));
    } else {
      std::fprintf(stderr,
                   "usage: geow2 [--data <cache>] [--seed <n>] [--frames <n> --screenshot <png> [--size <w> <h>]"
                   " [--stick <x> <y>] [--fire <x> <y>] [--bomb <step>]]\n");
      return 2;
    }
  }
  const bool headless = frames >= 0 && !screenshot.empty();
  // The GPU device needs the video subsystem even when no window is opened.
  if (!SDL_Init(headless ? SDL_INIT_VIDEO : (SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))) {
    SDL_Log("SDL_Init: %s", SDL_GetError());
    return 1;
  }
  Game game;
  if (!load(game, cache, seed)) return 1;
  SDL_GPUDevice* dev = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, false, nullptr);
  if (!dev) {
    SDL_Log("no GPU device: %s", SDL_GetError());
    return 1;
  }
  Renderer renderer;
  if (!renderer.init(dev, fs::path(SDL_GetBasePath() ? SDL_GetBasePath() : "") / "shaders")) return 1;
  const int rc =
      headless ? run_headless(dev, renderer, game, frames, sticks, bomb_step, screenshot, shot_w, shot_h) : run_window(dev, renderer, game);
  renderer.release();
  SDL_DestroyGPUDevice(dev);
  SDL_Quit();
  return rc;
}

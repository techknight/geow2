#pragma once

// The game: its data, one frame step of the simulation (sub_820366A0 and
// what the frame calls around it) and its picture.

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "app/renderer.h"
#include "data/flask.h"
#include "data/xtlo.h"
#include "extract/bytes.h"
#include "game/bullets.h"
#include "game/camera.h"
#include "game/deadline.h"
#include "game/game_math.h"
#include "game/font.h"
#include "game/gate.h"
#include "game/geoms.h"
#include "game/gravity_well.h"
#include "game/grid.h"
#include "game/hill.h"
#include "game/hud.h"
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
#include "game/wave_spawner.h"
#include "game/ui.h"
#include "game/ui_text.h"
#include "game/world.h"
#include "game/rotation.h"
#include "game/sequence.h"
#include "game/shots.h"
#include "game/spawn.h"


namespace geow2 {

constexpr float kStep = 1.0f / 60.0f;          // dat_8219D280 = 1.0 / 60.0 (sub_8205D538)
constexpr float kArenaHalfX = 300.0f;          // dat_82000CF4 (sub_82036250)
constexpr float kArenaHalfY = 200.0f;          // dat_82000CF0
constexpr float kShapeCapSize = 1.0f;          // dat_820006C4 (sub_82061908)

// A score table (0x370 bytes in the game block 0x82199470, one per mode;
// session only): entry 0 the last game, 1 the best this session, 2..9 the
// previous games, newest first.
struct ScoreEntry {
  int64_t score = 0;
  int multiplier = 1;
  std::string name;
};
struct ScoreTable {
  std::array<ScoreEntry, 10> entry{};
  // sub_82034B48.
  void insert(int64_t score, int multiplier, const std::string& name);
};

// One step's pad input, raw (XInput convention).
struct Sticks {
  int16_t move_x = 0, move_y = 0, fire_x = 0, fire_y = 0;  // raw, XInput convention
  uint8_t left_trigger = 0, right_trigger = 0;               // raw
};

// Everything the game keeps: the user's data, the playfield and its
// effects, the player, the mode's rules and spawners, and what each step
// produces for drawing.
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
  // The HUD: the font, its strings and the UI transform.
  Font font;
  HudText hud_text;
  Mat4 ui{};
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
  LineBatch debris_lines;                  // this step's line debris
  LineBatch linger_lines;                  // this step's lingering numbers
  // Enemies: the units of Data.bin, the playfield and Deadline's spawner.
  std::unique_ptr<Units> units;
  std::unique_ptr<World> world;
  DeadlineSpawner spawner;
  WaveSpawner waves;     // the other modes' spawner (0x8219DDA0)
  Sequence sequence;     // Sequence's levels (0x8219D3E0)
  float elapsed = 0.0f;  // dat_8219CE68
  bool over = false;     // dat_8219CE73: the game-over rule was met
  // The game's flow (dat_821993F0): 0 front end, 1 starting, 2 playing,
  // 3 paused, 4 ending; the end countdown (dat_821993D4) and its reason
  // (dat_821993D8: 1 quit, 2 game over, 3 profile change).
  int flow = 2;
  // dat_821D39F0: scales both blooms; 0 at boot until the intro is over.
  float bloom_level = 1.0f;
  float end_timer = 0.0f;
  int end_reason = 0;
  bool hint_pending = false;  // sub_82034408's achievement-hint test (+1 s)
  // The text slots by id, and the credits (table 0x8218D9E0: type, text).
  UiText text;
  std::vector<std::pair<int, std::string>> credits;
  // The CharMap record's models (mode icons, intermission marks), by field.
  std::map<std::string, ModelGeometry> charmap;
  // The single-player score tables, by mode (sub_82034C50).
  std::array<ScoreTable, 6> tables{};
  bool first_spawn = true;  // dat_8219CE70
  int after_death = 0;      // dat_8219D288: the last player died (sub_8204E7D8)
  // The player's life and the mode's rules.
  PlayerLife life;
  ModeRules rules;
  std::string plus_life_text = "Extra life";  // ID_X_PLUS_ONE_LIFE (0x83)
  std::string plus_bomb_text = "Extra bomb";  // ID_X_PLUS_ONE_BOMB (0x84)
  const UnitInfo* player_unit = nullptr;
  bool field_live = false;   // dat_8219CE7D: a player spawned since the last clear
  bool was_fading = false;   // dat_8219D3D0: a player was fading in last step
};

// Loads the user's data from the cache and sets up a game of `mode`.
bool load(Game& g, const std::filesystem::path& cache, int64_t seed, int mode);
// sub_82033148: a new game of `mode` (the game block reset, the mode's
// setup, the playfield and spawners reset); the flow goes to playing.
void start_game(Game& g, int mode);
// sub_82033610 / sub_82033638 / sub_82034588.
inline bool in_game(const Game& g) { return g.flow == 2 || g.flow == 3; }
inline bool ending(const Game& g) { return g.flow == 4 || 0.0f < g.end_timer; }
inline bool frozen(const Game& g) { return g.flow == 3; }
// sub_8207CA48: the single-player HUD into `out`.
void draw_game_hud(const Game& g, LineBatch& out);
// One simulation step (sub_820366A0) with this step's input.
void step(Game& g, const Sticks& s);
// The frame's picture into `target` (w x h).
void render(Renderer& r, SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* target, SDL_GPUTextureFormat format,
            uint32_t w, uint32_t h, const Game& g, const UiCanvas& ui);
// One simulation step and its light map update (one per step, as the
// original renders one frame per step).
void tick(Game& g, const Sticks& s, Renderer& r, SDL_GPUCommandBuffer* cmd);

}  // namespace geow2

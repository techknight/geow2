#pragma once

// The game: its data, one frame step of the simulation (sub_820366A0 and
// what the frame calls around it) and its picture.

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
  bool ended = false;    // the end handled (field cleared, results next)
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
// One simulation step (sub_820366A0) with this step's input.
void step(Game& g, const Sticks& s);
// The frame's picture into `target` (w x h).
void render(Renderer& r, SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* target, SDL_GPUTextureFormat format,
            uint32_t w, uint32_t h, const Game& g);
// One simulation step and its light map update (one per step, as the
// original renders one frame per step).
void tick(Game& g, const Sticks& s, Renderer& r, SDL_GPUCommandBuffer* cmd);

}  // namespace geow2

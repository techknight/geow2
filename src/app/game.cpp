#include "app/game.h"

namespace fs = std::filesystem;

namespace geow2 {
namespace {

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


// sub_82040818: whether the player's kills score. With lives, while the
// player is in the game, can come back free or has a life left; with the
// time limit, until time is up.
void update_scoring(Game& g) {
  const PlayerLife& p = g.life;
  if (g.rules.lives_mode == 0)
    g.world->scoring_open = p.lives >= 1 || p.alive() || p.free_spawn;
  else if (g.rules.lives_mode == 2)
    g.world->scoring_open = g.world->clock < g.rules.length;
}

// The rest of sub_820408D0 once the score has risen: an extra life and an
// extra bomb at each threshold (100000, then ten times the last), at most
// nine of each, with sound events 0x1A / 0x19 and a pop-up at the player
// (the life's 5 above and the bomb's 5 below when both come at once). As
// in the original, an extra life pulses the bombs' row (dat_8219D0C4) and
// an extra bomb the lives' row (dat_8219D0C0).
void score_awards(Game& g) {
  World& w = *g.world;
  PlayerLife& p = g.life;
  bool life = false, bomb = false;
  if ((g.rules.flags & 1) == 0 && p.life_at <= w.score) {
    if (p.lives < 9) ++p.lives;
    p.bombs_pulse = 1.0f;  // dat_820006C4
    p.life_at = p.life_at * 10;
    life = true;
  }
  if ((g.rules.flags & 2) == 0 && p.bomb_at <= w.score) {
    if (p.bombs < 9) ++p.bombs;
    p.lives_pulse = 1.0f;
    p.bomb_at = p.bomb_at * 10;
    bomb = true;
  }
  if (!life && !bomb) return;
  const float f = life && bomb ? 5.0f : 0.0f;  // dat_820006F0, dat_820006CC
  // Size 1.25 (dat_82000768), the player's colour index (0).
  if (life) w.popups.add_big(g.player.x, g.player.y - f, 1.25f, 0, g.plus_life_text);
  if (bomb) w.popups.add_big(g.player.x, g.player.y + f, 1.25f, 0, g.plus_bomb_text);
}

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
  // debris in the player's colour and a kill marker; no score, no sound 2.
  Killer k;
  k.x = killer.x;
  k.y = killer.y;
  k.vx = killer.vx;
  k.vy = killer.vy;
  k.kind = 2;
  replace_killer(w, killer);
  if (g.player_unit) {
    w.effects.trigger(g.player_unit->killed, g.player.x, g.player.y);
    const Mat4 m = placement_style_none(*g.gm, g.player.x, g.player.y, g.player.heading);
    w.shatter(g.player_unit, m, g.player.x, g.player.y, g.player.radius, k, g.tint);
  }
  w.death_markers.push_back({g.player.x, g.player.y});
  // (In Deadline, dat_8219CEF0 between 11 and 74 sets dat_8219CEF4 to 4:
  // with the HUD.)
  p.removed = true;
  w.players[0].removed = true;
  // No player is left (sub_8204E7D8): dat_8219D288 = 1.
  g.after_death = 1;
  update_scoring(g);
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
    if (g.rules.mode == 0) g.spawner.player_died(w, g.rules.length - g.elapsed);
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
  p.shield_hit = true;
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
  t.prev_x = t.x;
  t.prev_y = t.y;
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
  // King's first three zones come with the game's first spawn
  // (dat_8219CE70): at the centre, then two placed clear of the rest.
  if (g.first_spawn) {
    g.first_spawn = false;
    if (g.rules.mode == 5) {
      World& w = *g.world;
      w.add(std::make_unique<Hill>(w, 0.0f, 0.0f));
      for (int i = 0; i < 2; ++i) {
        const Vec2 at = hill_position(w);
        w.add(std::make_unique<Hill>(w, at.x, at.y));
      }
    }
  }
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


// The player list's walk (sub_820453E0 for the player): fading in first;
// once in, the update (sub_8204FF00: thrust, firing, the bomb, the timers)
// and the move; then the arena edge.
void update_player(Game& g, const Sticks& s) {
  World& w = *g.world;
  PlayerLife& p = g.life;
  if (!p.fading) {
    // The update starts by keeping where the player was (+0xBC / +0xC0).
    w.players[0].prev_x = g.player.x;
    w.players[0].prev_y = g.player.y;
  }
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
    if (player_thrust(g.player, move, *g.gm)) player_exhaust(w, g.player);
    const size_t fired = g.bullets.size();
    // sub_8204EFD0: never in Pacifism; in King only inside a zone.
    const bool may_fire =
        g.rules.mode == 5 ? in_hill(w, g.player.radius, g.player.x, g.player.y) : g.rules.mode != 4;
    player_fire_step(g.player, g.weapon, fire, kStep, *g.gm, may_fire, g.bullets);
    for (size_t i = fired; i < g.bullets.size(); ++i) w.enlist(g.bullets[i]);
    // The larger trigger (axes 8 and 9) sets off a bomb: a blast from the
    // player's position, and the spawner backs off (sub_8203E4B0).
    const float lt = trigger_axis(s.left_trigger), rt = trigger_axis(s.right_trigger);
    if (player_bomb(p, lt - rt < 0.0f ? rt : lt, g.rules)) {
      w.add(std::make_unique<Shockwave>(w, 400.0f, 533.2f, g.player.x, g.player.y, 0));  // dat_82000F88, dat_82001C68
      g.spawner.bombed(w);
    }
    player_timers(p, kStep);
    integrate(g.player, kStep);
  }
  Vec2 n;
  if (arena_contact(g.player.radius, kArenaHalfX, kArenaHalfY, g.player.x, g.player.y, n)) player_wall(g.player, n);
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
  // The debris batches (drawn with the same presets, before the objects).
  out.body.insert(out.body.end(), g.debris_lines.body.begin(), g.debris_lines.body.end());
  out.caps.insert(out.caps.end(), g.debris_lines.caps.begin(), g.debris_lines.caps.end());
}

// sub_82034408(reason): the end countdown, 1.5 seconds (dat_82000C90), a
// second more when an achievement hint is on screen (single player: King
// with no bomb or shield hit and 10 or more zones; Waves likewise with
// more than 3 rocket walls; Deadline's and Pacifism's hints need counters
// not ported yet).
void arm_end(Game& g, int reason) {
  if (!in_game(g) || 0.0f < g.end_timer) return;
  g.end_timer = 1.5f;
  g.end_reason = reason;
  const PlayerLife& p = g.life;
  bool hint = false;
  if (g.rules.mode == 5)
    hint = !p.bomb_used && !p.shield_hit && g.world->zones_captured >= 10;
  else if (g.rules.mode == 3)
    hint = !p.bomb_used && !p.shield_hit && g.waves.walls >= 4;
  g.hint_pending = hint;
  if (hint) g.end_timer = g.end_timer + 1.0f;
}

}  // namespace

bool load(Game& g, const fs::path& cache, int64_t seed, int mode) {
  g.rules = mode_rules(mode);
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
  for (uint32_t r = 0; r < g.db->records().size(); ++r) {
    if (g.db->types()[g.db->records()[r].type].name != "ProgressionLevel") continue;
    const FlaskValue v = g.db->decode(r);
    const FlaskStruct* s = as_struct(&v);
    const FlaskValue* level = s ? s->find("level") : nullptr;
    const FlaskValue* script = s ? s->find("script") : nullptr;
    if (!level || !script || !std::holds_alternative<std::string>(script->v)) continue;
    int index = -1;
    if (auto* i = std::get_if<int64_t>(&level->v)) index = int(*i);
    else if (auto* u = std::get_if<uint64_t>(&level->v)) index = int(*u);
    else if (auto* d = std::get_if<double>(&level->v)) index = int(*d);
    g.sequence.scripts.emplace_back(index, std::get<std::string>(script->v));
  }
  g.world->scored = [&g]() { score_awards(g); };
  g.world->achieve = [&g](uint32_t bit, int flags) { award_achievement(g, bit, flags); };
  g.sequence.achieve = [&g](uint32_t bit, int flags) { award_achievement(g, bit, flags); };
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
    g.font = Font::from_image(image);
    g.geom_shapes = GeomShapes::from_image(image);
  } catch (const std::exception& e) {
    SDL_Log("cannot read the shot and geom shapes: %s", e.what());
    return false;
  }
  for (uint32_t r = 0; r < g.db->records().size(); ++r) {
    if (g.db->types()[g.db->records()[r].type].name != "CharMap") continue;
    const FlaskValue v = g.db->decode(r);
    if (const FlaskStruct* s = as_struct(&v))
      for (const auto& [name, field] : s->fields)
        if (const auto* cref = std::get_if<FlaskRef>(&field.v)) {
          const FlaskValue cmodel = g.db->decode(cref->record);
          if (const FlaskStruct* m = as_struct(&cmodel)) g.charmap[name] = build_model_geometry(*m, *g.gm);
        }
    break;
  }
  // The credits: 12-byte entries (type, text pointer, y) up to type 5.
  try {
    const auto image = read_file(cache / "xex" / "image.bin");
    auto be32 = [&](size_t o) {
      return uint32_t(image[o]) << 24 | uint32_t(image[o + 1]) << 16 | uint32_t(image[o + 2]) << 8 | uint32_t(image[o + 3]);
    };
    for (size_t o = 0x8218D9E0 - 0x82000000; o + 12 <= image.size(); o += 12) {
      const uint32_t type = be32(o), p = be32(o + 4);
      if (type == 5 || p < 0x82000000 || p - 0x82000000 >= image.size()) break;
      std::string s;
      for (size_t k = p - 0x82000000; k < image.size() && image[k]; ++k) s.push_back(char(image[k]));
      g.credits.emplace_back(int(type), s);
    }
  } catch (const std::exception&) {
  }
  g.world->geom_shapes = &g.geom_shapes;
  g.world->shots = &g.bullets;
  g.ui = ui_transform(*g.gm);
  // The HUD's strings in English (the slot conversion keeps Latin-1).
  try {
    const TextTable text(read_file(cache / "files" / "Text-en.loc"), "english");
    auto narrow = [](const std::u16string& w) {
      std::string out;
      for (char16_t c : w) out.push_back(c <= 0xFF ? char(c) : '?');
      return out;
    };
    if (auto t = text.find("ID_X_SCORE")) g.hud_text.score = narrow(*t);
    if (auto t = text.find("ID_X_HIGH_SCORE")) g.hud_text.high_score = narrow(*t);
    if (auto t = text.find("ID_X_BONUS_MULTIPLIER")) g.world->bonus_multiplier_text = narrow(*t);
    if (auto t = text.find("ID_X_PLUS_ONE_LIFE")) g.plus_life_text = narrow(*t);
    if (auto t = text.find("ID_X_PLUS_ONE_BOMB")) g.plus_bomb_text = narrow(*t);
    g.text = UiText::load(read_file(cache / "xex" / "image.bin"), text);
  } catch (const std::exception& e) {
    SDL_Log("cannot read the text (%s): the HUD uses its built-in labels", e.what());
  }
  // The score tables start empty (sub_82034640: score 0, multiplier 1).
  start_game(g, mode);
  return true;
}

void start_game(Game& g, int mode) {
  World& w = *g.world;
  // The game block's reset (sub_820348E8) and the mode's setup.
  g.rules = mode_rules(mode);
  g.elapsed = 0.0f;
  g.over = false;
  g.first_spawn = true;
  g.after_death = 0;
  g.field_live = false;
  g.was_fading = false;
  g.end_timer = 0.0f;
  g.end_reason = 0;
  g.hint_pending = false;
  g.earned = 0;
  g.life = PlayerLife{};
  // sub_82032FF8: lives and bombs into the player record.
  g.life.lives = g.rules.start_lives;
  g.life.bombs = g.rules.start_bombs;
  g.player = PlayerMotion{};
  g.weapon = PlayerWeapon{};
  g.bullets.clear();
  // sub_82036250: the object lists emptied, the arena and the grid reset,
  // the spawners reset (sub_8203E538: Deadline's, the shared one, then
  // Sequence's), Pacifism's edges cleared.
  w.new_game();
  w.players.assign(1, PlayerTarget{});
  w.players[0].radius = 5.0f;
  w.players[0].removed = true;
  w.mode = g.rules.mode;
  w.king_mode = g.rules.mode == 5;
  g.grid.reset(kArenaHalfX, kArenaHalfY);
  g.spawner = DeadlineSpawner{};
  g.waves.reset(w);
  g.sequence.reset(w);
  // Sequence holds the game's end back until its own (dat_8219CE72,
  // cleared by its setup).
  g.sequence.force_over = false;
  g.sequence.allow_end = g.rules.mode != 2;
  g.sequence.level_timer = 0.0f;
  g.flow = 2;
}

void award_achievement(Game& g, uint32_t bit, int flags) {
  if (g.earned & bit) return;
  g.earned |= bit;
  if (g.achieved & bit) return;
  // (Sound event 0x24 unless flags & 1.)
  if (!(flags & 2)) {
    g.achieved |= bit;
    if (g.unlocked) g.unlocked(bit);
  }
}

void ScoreTable::insert(int64_t score, int multiplier, const std::string& name) {
  entry[0] = {score, multiplier, name};
  if (entry[1].score < score) entry[1] = entry[0];
  if (score > 0) {
    for (size_t i = 9; i > 2; --i) entry[i] = entry[i - 1];
    entry[2] = entry[0];
  }
}

void step(Game& g, const Sticks& s) {
  World& w = *g.world;
  w.bomb_kills = 0;
  // The spawner (sub_8203E7E8) runs while a player is in the game:
  // Deadline's own, the others' shared one (Sequence's is not ported);
  // then the clock (sub_820366A0).
  if (frozen(g)) return;
  if (ending(g)) {
    // (No spawner while the game ends.)
  } else if (g.rules.mode == 2) {
    Sequence::Inputs in;
    in.alive = g.life.alive();
    in.over = g.over;
    in.after_death = &g.after_death;
    in.respawn_wait = &g.life.respawn_wait;
    in.level_time = g.rules.level_time;
    g.sequence.step(w, in);
  } else if (g.life.alive()) {
    if (g.rules.mode == 0)
      g.spawner.step(w, g.elapsed);
    else
      g.waves.step(w, g.rules.mode, g.life.bomb_used || g.life.shield_hit, g.after_death);
  }
  g.elapsed = kStep + g.elapsed;
  w.clock = g.elapsed;
  update_scoring(g);
  // Last step's kill markers go (sub_82043368).
  w.death_markers.clear();

  if (g.life.alive()) update_player(g, s);
  PlayerTarget& target = w.players[0];
  target.x = g.player.x;
  target.y = g.player.y;
  target.vx = g.player.vx;
  target.vy = g.player.vy;
  target.fading = g.life.fading;
  target.push_vx = target.push_vy = 0.0f;
  // Then the other objects, shots included, in creation order (the player
  // list is walked first, so shots fired this step move this step).
  w.update_entities();
  // Shots against enemies (sub_820523B0).
  shot_collisions(w, g.bullets);
  std::erase_if(g.bullets, [](const Bullet& b) { return b.removed; });
  // Players crossing gates' beams (sub_820476F0).
  gate_crossings(w);
  // Geoms are pulled in and picked up (sub_8204D410).
  collect_geoms(w);
  // The HUD rows' pulses fade (0.9 a step, 0x82000D70).
  g.life.lives_pulse = g.life.lives_pulse * 0.9f;
  g.life.bombs_pulse = g.life.bombs_pulse * 0.9f;
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
  // Respawns and the game over, while a game runs and is not ending.
  if (in_game(g) && !ending(g)) {
    respawn(g);
    update_scoring(g);
    // Game over: with lives, when no player is left and none can come
    // back; with the time limit, when time is up; or when Sequence says so
    // (dat_8219CE71). Sounds 1 and 0x25; unless Sequence holds the end back
    // (dat_8219CE72) the field is cleared and the end counts down
    // (sub_820364D0, sub_82034408(2)).
    const PlayerLife& p = g.life;
    bool end = g.sequence.force_over;
    if (g.rules.lives_mode == 0)
      end = end || (p.alive() ? 1 : 0) + (p.free_spawn ? 1 : 0) + p.lives < 1;
    else if (g.rules.lives_mode == 2)
      end = end || g.rules.length <= w.clock;
    if (end) {
      g.over = true;
      if (g.sequence.allow_end) {
        clear_field(g);
        arm_end(g, 2);
      }
    }
  }

  // The grid (sub_8205C8F8, then sub_8205B880 on worker threads): pushes
  // gathered from the entities, the spring step, and the line pass from
  // the positions the step read. Bullets push with radius +0x8C and
  // strength 2.0 (sub_8205B578).
  GridInputs in;
  // Active wells light the grid (sub_8205B578).
  well_grid_lights(w, in.lights);
  // The specials: a fading-in player pulls, the first blast pushes.
  if (g.life.alive() && g.life.fading) in.fading_player = Vec2{g.player.x, g.player.y};
  for (const auto& e : w.entities)
    if (e->type == 0xF) {
      in.special = Vec2{e->x, e->y};
      break;
    }
  // King's zones: radius, strength 4.
  in.pushes.emplace_back();
  hill_grid_pushes(w, in.pushes.back());
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
  well_particle_forces(w);
  g.particle_quads.clear();
  w.particles.step(w.particle_tuning, kStep, w.half_x, w.half_y, w.attractors, w.repulsors, g.particle_quads,
                   g.lights);

  // The line debris: drawn by the render set-up (sub_82074600), then
  // stepped on the render worker (sub_82074B28) with the stars.
  g.debris_lines.clear();
  w.debris.draw(g.debris_lines, g.lights);
  w.debris.step(w.smash, kStep, w.half_x, w.half_y);
  // The lingering numbers likewise (sub_8205A8F0, then sub_8205A7F0).
  g.linger_lines.clear();
  w.popups.draw(g.font, g.linger_lines);
  w.popups.step(kStep);

  // The stars' step (sub_82075548, on the render worker each frame).
  g.stars.update(*g.gm, 1.0f, g.star_vertices);

  // What the wells' pulls and shoves did to the player's velocity.
  g.player.vx = g.player.vx + target.push_vx;
  g.player.vy = g.player.vy + target.push_vy;

  // Entities removed this step are deleted, the player object with them.
  w.purge();
  if (g.life.removed) g.life.present = false;

  // The achievements checked every step of a game (0x82037B0C..):
  // Treaty (King: 30 zones, no bomb, no shield hit), Surf (Waves: 8 rocket
  // walls likewise), Phobia (Waves: 1000 points at multiplier 1), Rebound
  // (Deadline: 75 bounced-shot kills), Magpie (multiplier 500).
  if (in_game(g)) {
    const PlayerLife& p = g.life;
    if (g.rules.mode == 5) {
      if (!p.bomb_used && !p.shield_hit && w.zones_captured > 29) award_achievement(g, 0x002, 0);
    } else if (g.rules.mode == 3) {
      if (!p.bomb_used && !p.shield_hit && g.waves.walls > 7) award_achievement(g, 0x040, 0);
      if (w.score > 999 && w.multiplier == 1) award_achievement(g, 0x100, 0);
    } else if (g.rules.mode == 0 && w.bounce_kills > 0x4A) {
      award_achievement(g, 0x200, 0);
    }
    if (w.multiplier > 499) award_achievement(g, 0x080, 0);
  }
}

void draw_game_hud(const Game& g, LineBatch& ui) {
  // The high score: the session's best for the mode, or this game's score
  // when higher.
  HudState hud;
  if (g.rules.mode >= 0 && g.rules.mode < 6) hud.high_score = g.tables[size_t(g.rules.mode)].entry[1].score;
  hud.score = g.world->score;
  hud.multiplier = g.world->multiplier;
  hud.bombs = g.life.bombs;
  hud.lives = g.life.lives;
  hud.lives_pulse = g.life.lives_pulse;
  hud.bombs_pulse = g.life.bombs_pulse;
  hud.start_lives = g.rules.start_lives;
  hud.mode = g.rules.mode;
  hud.level_time = g.rules.level_time;
  hud.level_timer = g.sequence.level_timer;
  hud.clock = g.elapsed;
  hud.length = g.rules.length;
  hud.lives_mode = g.rules.lives_mode;
  draw_hud(g.font, g.hud_text, hud, ui);
}

void render(Renderer& r, SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* target, SDL_GPUTextureFormat format,
            uint32_t w, uint32_t h, const Game& g, const UiCanvas& ui) {
  static LineBatch front;
  foreground(g, front);
  r.draw(cmd, target, format, w, h, camera_view_projection(g.camera, *g.gm), g.star_vertices,
         g.grid_visible ? &g.grid_lines : nullptr, g.walls, g.particle_quads, g.linger_lines, front, ui, g.ui,
         camera_view_projection(CameraState{}, *g.gm));
}

// One simulation step and its light map update (one per step, as the
// original renders one frame per step).
void tick(Game& g, const Sticks& s, Renderer& r, SDL_GPUCommandBuffer* cmd) {
  step(g, s);
  r.update_light_map(cmd, g.warp_mesh, g.lights);
}

}  // namespace geow2

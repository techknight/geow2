#include "app/game.h"

namespace fs = std::filesystem;

namespace geow2 {
namespace {

void draw_edges(const Game& g, LineBatch& out);

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


bool any_alive(const Game& g) {
  for (int k = 0; k < g.ship_count; ++k)
    if (g.ships[size_t(k)].life.alive()) return true;
  return false;
}

// The stick input of joined index j (none when there is no such pad).
const Sticks& pad(std::span<const Sticks> pads, int j) {
  static const Sticks none;
  return j >= 0 && size_t(j) < pads.size() ? pads[size_t(j)] : none;
}

// sub_82040818 per record: whether its kills score. With lives, while the
// record has a life left or one of its ships is in the game or can come
// back free; with the time limit, until time is up.
void update_scoring(Game& g) {
  for (int r = 0; r < g.record_count; ++r) {
    if (g.rules.lives_mode == 0) {
      bool open = g.recs[size_t(r)].lives >= 1;
      for (int k = 0; k < g.ship_count; ++k) {
        const Ship& s = g.ships[size_t(k)];
        if (s.record == r && (s.life.alive() || s.life.free_spawn)) open = true;
      }
      g.world->scoring_open[size_t(r)] = open;
    } else if (g.rules.lives_mode == 2) {
      g.world->scoring_open[size_t(r)] = g.world->clock < g.rules.length;
    }
  }
}

// The rest of sub_820408D0 once the score has risen: an extra life and an
// extra bomb at each threshold (100000, then ten times the last), at most
// nine of each, for the record `r` the kill went to, with sound events
// 0x1A / 0x19 and a pop-up at the credited player `k` (the life's 5 above
// and the bomb's 5 below when both come at once). As
// in the original, an extra life pulses the bombs' row (dat_8219D0C4) and
// an extra bomb the lives' row (dat_8219D0C0).
void score_awards(Game& g, int r, size_t k) {
  World& w = *g.world;
  LifeRecord& p = g.recs[size_t(r)];
  const int64_t score = w.records[size_t(r)].score;
  bool life = false, bomb = false;
  if ((g.rules.flags & 1) == 0 && p.life_at <= score) {
    if (p.lives < 9) ++p.lives;
    p.bombs_pulse = 1.0f;  // dat_820006C4
    p.life_at = p.life_at * 10;
    life = true;
    w.play(audio::Event::kPickupExtralife);
  }
  if ((g.rules.flags & 2) == 0 && p.bomb_at <= score) {
    if (p.bombs < 9) ++p.bombs;
    p.lives_pulse = 1.0f;
    p.bomb_at = p.bomb_at * 10;
    bomb = true;
    w.play(audio::Event::kPickupSmartbomb);
  }
  if (!life && !bomb) return;
  const float f = life && bomb ? 5.0f : 0.0f;  // dat_820006F0, dat_820006CC
  // Size 1.25 (dat_82000768), the player's colour index (+0xA0).
  const Ship& s = g.ships[k];
  if (life) w.popups.add_big(s.motion.x, s.motion.y - f, 1.25f, s.colour, g.plus_life_text);
  if (bomb) w.popups.add_big(s.motion.x, s.motion.y + f, 1.25f, s.colour, g.plus_bomb_text);
}

// The player's death (vtable slot 9, sub_8204E988), with the enemy that
// touched it as the killer.
void kill_player(Game& g, size_t ki, Entity& killer) {
  World& w = *g.world;
  Ship& s = g.ships[ki];
  PlayerLife& p = s.life;
  const PlayerMotion& m = s.motion;
  // In single player (dat_8219D275 clear) the slot keeps where the player
  // died, the next spawn point; the mode's delay; sound event 4.
  if (!g.multiplayer) {
    p.spawn_x = m.x;
    p.spawn_y = m.y;
  }
  p.respawn_wait = g.rules.respawn_delay;
  ++g.recs[size_t(s.record)].deaths;
  // The killer gives way to its ghost (sub_82044FA8), then
  // sub_82046010(player, killer, 0x1E): the player's killed effect, line
  // debris in the player's colour and a kill marker; no score, no sound 2.
  Killer k;
  k.x = killer.x;
  k.y = killer.y;
  k.vx = killer.vx;
  k.vy = killer.vy;
  k.kind = 2;
  k.player = ki;
  replace_killer(w, killer);
  const ShipLook& look = g.looks[size_t(s.model_look)];
  if (look.unit) {
    w.effects.trigger(look.unit->killed, m.x, m.y);
    const Mat4 mm = placement_style_none(*g.gm, m.x, m.y, m.heading);
    w.shatter(look.unit, mm, m.x, m.y, m.radius, k, g.looks[size_t(s.colour_look)].tint);
  }
  w.death_markers.push_back({m.x, m.y});
  // (In Deadline, dat_8219CEF0 between 11 and 74 sets dat_8219CEF4 to 4:
  // with the HUD.)
  p.removed = true;
  w.players[ki].removed = true;
  // Deadline: the Rebound hint comes back for 4 s after a death between
  // 11 and 74 bounced-shot kills.
  if (g.rules.mode == 0 && 10 < w.bounce_kills && w.bounce_kills < 75) w.bounce_hint = 4.0f;  // dat_820006F4
  w.play(audio::Event::kPlayerDie);
  // No player is left (sub_8204E7D8): dat_8219D288 = 1.
  if (!any_alive(g)) g.after_death = 1;
  update_scoring(g);
}

// A unit touching the player: slot 12 (sub_8204E8A0) passes it to slot 7
// (sub_8204EEA8) as a kind 2 killer.
void player_touched(Game& g, const Touch& t) {
  if (g.rules.mode == 7 && (t.enemy->type == 0x13 || t.enemy->type == 3)) return;
  World& w = *g.world;
  Ship& s = g.ships[t.player];
  PlayerLife& p = s.life;
  // dat_8219F500 (never set here) and the bomb wait guard the player.
  if (!(p.bomb_wait <= 0.0f)) return;
  if (p.shield <= 0.0f) {
    kill_player(g, t.player, *t.enemy);
    if (g.rules.mode == 0) g.spawner.player_died(w, g.rules.length - g.elapsed);
    return;
  }
  // The shield destroys what touches it (sound event 9): a kind 2 kill by
  // the player for touchable units (no score), removal for flag 8.
  LifeRecord& r = g.recs[size_t(s.record)];
  Entity& e = *t.enemy;
  if (e.flags & kEntityTouchable) {
    Killer k;
    k.x = s.motion.x;
    k.y = s.motion.y;
    k.vx = s.motion.vx;
    k.vy = s.motion.vy;
    k.kind = 2;
    k.player = t.player;
    e.kill(w, k);
    ++r.shield_kills;
    w.play(audio::Event::kPlayerShieldedHitEnemy);
  } else if (e.flags & 0x8) {
    w.remove(e);
  }
  r.shield_hit = true;
}

// A new player object (sub_82051448) at the slot's spawn point: radius 5
// (dat_820006F0), heading -pi/2 (dat_820010F4), at rest, fading in; it
// joins the end of the player list. The slot's gate chain stays.
void spawn_player(Game& g, size_t k) {
  Ship& s = g.ships[k];
  s.motion = PlayerMotion{};
  s.motion.x = s.life.spawn_x;
  s.motion.y = s.life.spawn_y;
  s.motion.radius = 5.0f;
  s.motion.heading = -1.57079637f;
  s.weapon = PlayerWeapon{};
  player_spawned(s.life);
  PlayerTarget& t = g.world->players[k];
  const int chain = t.gate_chain;
  const float chain_time = t.gate_chain_time;
  t = PlayerTarget{};
  t.x = s.motion.x;
  t.y = s.motion.y;
  t.prev_x = t.x;
  t.prev_y = t.y;
  t.radius = s.motion.radius;
  t.record = s.record;
  t.colour = s.colour;
  t.gate_chain = chain;
  t.gate_chain_time = chain_time;
  g.player_list.push_back(k);
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

// The spawn block of sub_820366A0 (0x82037504..0x82037750).
void respawn(Game& g) {
  World& w = *g.world;
  // With no player in the game and no slot able to come back within a
  // second, every slot waits a second.
  bool alive = false, soon = false;
  for (int k = 0; k < g.ship_count; ++k) {
    const Ship& s = g.ships[size_t(k)];
    alive = alive || s.life.alive();
    const bool can = g.rules.lives_mode != 0 || s.life.free_spawn || g.recs[size_t(s.record)].lives > 0;
    soon = soon || (can && s.life.respawn_wait < 1.0f);
  }
  if (!alive && !soon)
    for (int k = 0; k < g.ship_count; ++k) g.ships[size_t(k)].life.respawn_wait = 1.0f;
  // Each slot without an object counts down, then spawns: free in the
  // time-limit modes, else its free spawn or one of its record's lives.
  // With two or more ships each spawn sounds by player number
  // (MultiPlayerSpawn0..3), else one sound 3 after the loop.
  const bool numbered = g.multiplayer && g.ship_count >= 2;
  bool spawned = false;
  for (int k = 0; k < g.ship_count; ++k) {
    Ship& s = g.ships[size_t(k)];
    PlayerLife& p = s.life;
    if (p.present) continue;
    const float left = p.respawn_wait - kStep;
    p.respawn_wait = left < 0.0f ? 0.0f : left;
    if (!(p.respawn_wait <= 0.0f)) continue;
    if (g.rules.lives_mode == 0) {
      int& lives = g.recs[size_t(s.record)].lives;
      if (p.free_spawn)
        p.free_spawn = false;
      else if (lives > 0)
        --lives;
      else
        continue;
    }
    spawn_player(g, size_t(k));
    if (numbered) w.play(audio::Event(uint8_t(audio::Event::kMultiPlayerSpawn0) + uint8_t(s.number & 3)));
    spawned = true;
    g.field_live = true;
    // King's first three zones come with the game's first spawn
    // (dat_8219CE70): at the centre, then two placed clear of the rest.
    if (g.first_spawn) {
      g.first_spawn = false;
      if (g.rules.mode == 5) {
        w.add(std::make_unique<Hill>(w, 0.0f, 0.0f));
        for (int i = 0; i < 2; ++i) {
          const Vec2 at = hill_position(w);
          w.add(std::make_unique<Hill>(w, at.x, at.y));
        }
      }
    }
  }
  if (spawned && !numbered) w.play(audio::Event::kPlayerSpawn);
}

// A player starting to fade in, single player (sub_820366A0): 40 rounds of
// 64 blue particles (speed up to 25, depth times 0.9) and 8 red ones
// (speed up to 5) from its position.
void spawn_burst(Game& g, const Ship& s) {
  World& w = *g.world;
  const float x = s.motion.x, y = s.motion.y;
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


// The player list's walk (sub_820453E0 for a player): fading in first;
// once in, the update (sub_8204FF00: thrust, firing, the bomb, the timers)
// and the move; then the arena edge. The pilot's pad moves (+0x98), the
// copilot's fires (+0x9C); each trigger is the larger of the two pads'.
void update_player(Game& g, size_t k, std::span<const Sticks> pads) {
  World& w = *g.world;
  Ship& sh = g.ships[k];
  PlayerLife& p = sh.life;
  PlayerMotion& m = sh.motion;
  if (!p.fading) {
    // The update starts by keeping where the player was (+0xBC / +0xC0).
    w.players[k].prev_x = m.x;
    w.players[k].prev_y = m.y;
  }
  if (p.fading) {
    p.fade = std::fmaf(kStep, 1.5f, p.fade);  // dat_82000C90
    if (p.fade >= 1.0f) {
      p.fade = 1.0f;
      p.fading = false;
    }
  }
  if (!p.fading) {
    const Sticks& mp = pad(pads, sh.pilot);
    const Sticks& fp = pad(pads, sh.copilot);
    const Vec2 move = stick_vector(stick_axis(mp.move_x, false), stick_axis(mp.move_y, true));
    const Vec2 fire = stick_vector(stick_axis(fp.fire_x, false), stick_axis(fp.fire_y, true));
    if (player_thrust(m, move, *g.gm)) player_exhaust(w, m);
    const size_t fired = g.bullets.size();
    // sub_8204EFD0: never in Pacifism; in King only inside a zone.
    const bool may_fire = g.rules.mode == 5 ? in_hill(w, m.radius, m.x, m.y) : g.rules.mode != 4;
    const bool upgraded = 0.0f < sh.weapon.upgrade_time;
    player_fire_step(m, sh.weapon, fire, kStep, *g.gm, may_fire, g.bullets);
    for (size_t i = fired; i < g.bullets.size(); ++i) {
      g.bullets[i].owner = k;
      w.enlist(g.bullets[i]);
    }
    if (g.bullets.size() > fired) w.play(upgraded ? audio::Event::kFireSpecial : audio::Event::kFireNormal);
    // The larger trigger (axes 8 and 9) sets off a bomb: a blast from the
    // player's position, and the spawner backs off (sub_8203E4B0).
    const float lt = std::max(trigger_axis(mp.left_trigger), trigger_axis(fp.left_trigger));
    const float rt = std::max(trigger_axis(mp.right_trigger), trigger_axis(fp.right_trigger));
    const int bomb = player_bomb(p, g.recs[size_t(sh.record)], lt - rt < 0.0f ? rt : lt, g.rules);
    if (bomb == 1) {
      w.play(audio::Event::kFireSmartbomb);
      static_cast<Shockwave*>(w.add(std::make_unique<Shockwave>(w, 400.0f, 533.2f, m.x, m.y, 0)))->player =
          k;  // dat_82000F88, dat_82001C68
      g.spawner.bombed(w);
    } else if (bomb == 2) {
      w.play(audio::Event::kBombDryFire);
    }
    if (player_timers(p, kStep)) w.play(audio::Event::kShieldOff);
    integrate(m, kStep);
  }
  Vec2 n;
  if (arena_contact(m.radius, kArenaHalfX, kArenaHalfY, m.x, m.y, n)) {
    player_wall(m, n);
    // sub_8204E908: the wall sound, held off for two updates.
    if (p.wall_wait < 1) w.play(audio::Event::kHitWall);
    p.wall_wait = 2;
  }
}

// The player's draw (vtable slot 2, sub_82050F30): the body (part 0) and,
// while +0x90 is set, the shield (part 1), in the player's colour. Fading
// in, two growing ghosts as the units' (alpha folded, size unfolded).
void draw_player(const Game& g, const Ship& sh, LineBatch& out) {
  const PlayerLife& p = sh.life;
  if (!p.alive()) return;
  const PlayerMotion& pm = sh.motion;
  const ModelGeometry& model = g.looks[size_t(sh.model_look)].model;
  const float* tint = g.looks[size_t(sh.colour_look)].tint;
  const Mat4 m = placement_style_none(*g.gm, pm.x, pm.y, pm.heading);
  if (!p.fading) {
    draw_model_part(out, model, 0, m, tint);
    if (p.shield_shown) draw_model_part(out, model, 1, m, tint);
    return;
  }
  for (int i = 0; i < 2; ++i) {
    const float t = float(std::fmod(double(p.fade * 3.0f), 1.0)) + float(i);  // sub_8212FC00
    float f = t;
    if (f >= 1.0f) f = float(2.0 - double(f));
    if (f < 0.025f) continue;  // dat_82000D64
    const float s = std::fmaf(t, 1.4f, 0.1f);  // dat_82000FF8, dat_82000714
    Mat4 scaled = m;
    for (int r = 0; r < 3; ++r)
      for (int c = 0; c < 4; ++c) scaled.m[r * 4 + c] = m.m[r * 4 + c] * s;
    const float colour[4] = {tint[0], tint[1], tint[2], tint[3] * f};
    draw_model_part(out, model, 0, scaled, colour);
    if (p.shield_shown) draw_model_part(out, model, 1, scaled, colour);
  }
}

// The foreground lines: the enemies' models, the shots' shapes
// (sub_82061908) in their colour and the players, each line coloured as
// sub_82061DA0 does (line colour times the draw colour) with the model's
// cap size.
void foreground(const Game& g, LineBatch& out) {
  out.clear();
  // The pre-render pass's list order: enemies, shots, then the players.
  g.world->draw_entities(out);
  // The glow sprite of the shots is not drawn yet.
  for (const Bullet& b : g.bullets)
    for (const Segment& s : bullet_segments(b, g.bullet_shape, *g.gm))
      emit_line(out, kLineWidthUnit, kShapeCapSize, s.a, s.b, b.colour);
  for (size_t k : g.player_list) draw_player(g, g.ships[k], out);
  // Pacifism's edge marks (sub_8204FBA8, after the player's list).
  draw_edges(g, out);
  // The debris batches (drawn with the same presets, before the objects).
  out.body.insert(out.body.end(), g.debris_lines.body.begin(), g.debris_lines.body.end());
  out.caps.insert(out.caps.end(), g.debris_lines.caps.begin(), g.debris_lines.caps.end());
}

// sub_82034408(reason): the end countdown, 1.5 seconds (dat_82000C90), a
// second more when an achievement hint is on screen (single player: King
// with no bomb or shield hit and 10 or more zones; Waves likewise with
// more than 3 rocket walls; Deadline's Rebound line showing; Pacifism with
// 25 or more edge marks while Wax On or Wax Off is still to come).
// sub_8204F7C8: the ship within 10 of an edge marks that edge's cell (by
// its position along it); once all 100 are marked, touching a marked cell
// clears it, counting on to 200.
void track_edges(Game& g) {
  World& w = *g.world;
  const float hx = w.half_x, hy = w.half_y;
  const PlayerMotion& m = g.ships[0].motion;
  const float x = m.x, y = m.y, r = m.radius;
  const float ex = (hx - r) - 10.0f, ey = (hy - r) - 10.0f;  // dat_8200070C
  const int ix = std::clamp(int(((hx + x) / (hx * 2.0f)) * 30.0f), 0, 29);
  const int iy = std::clamp(int(((hy + y) / (hy * 2.0f)) * 20.0f), 0, 19);
  const bool marking = g.edge_count < 100;
  auto touch = [&](bool& cell) {
    if (marking) {
      if (cell) return;
      ++g.edge_count;
      cell = true;
      if (g.edge_count >= 25) w.play(audio::Event::kWaxOnClick);
    } else {
      if (!cell) return;
      ++g.edge_count;
      cell = false;
      w.play(audio::Event::kWaxOffClick);
    }
  };
  if (y < -ey) touch(g.edge_top[size_t(ix)]);
  if (y > ey) touch(g.edge_bottom[size_t(ix)]);
  if (x < -ex) touch(g.edge_left[size_t(iy)]);
  if (x > ex) touch(g.edge_right[size_t(iy)]);
}

// sub_8204FBA8 (single player): from 25 marks, a 16-long dash on the
// arena border for each marked cell (cap size 0.3), in the normal colour,
// the selection colour times 2 from 100.
void draw_edges(const Game& g, LineBatch& out) {
  if (g.multiplayer || g.rules.mode != 4 || (!in_game(g) && !ending(g)) || (g.achieved & 0xC00) == 0xC00 || g.edge_count < 25) return;
  const UiPalette p = UiPalette::defaults();
  const Rgba c = g.edge_count >= 100 ? scaled(p[kUiSelection], 2.0f) : p[kUiNormal];
  const float hx = g.world->half_x, hy = g.world->half_y;
  {
    const float half = std::fmaf(hx, 0.0333333351f, -2.0f);  // dat_82001E90
    for (int i = 0; i < 30; ++i) {
      const float m = std::fmaf((hx - -hx) * (float(i) + 0.5f), 0.0333333351f, -hx);
      const float x0 = m - half, x1 = half + m;
      if (g.edge_top[size_t(i)]) emit_line(out, kLineWidthUnit, 0.3f, {x0, -hy, 0, 1}, {x1, -hy, 0, 1}, c.data());
      if (g.edge_bottom[size_t(i)]) emit_line(out, kLineWidthUnit, 0.3f, {x0, hy, 0, 1}, {x1, hy, 0, 1}, c.data());
    }
  }
  {
    const float half = std::fmaf(hy, 0.05f, -2.0f);  // dat_82000D60
    for (int i = 0; i < 20; ++i) {
      const float m = std::fmaf((hy - -hy) * (float(i) + 0.5f), 0.05f, -hy);
      const float y0 = m - half, y1 = half + m;
      if (g.edge_left[size_t(i)]) emit_line(out, kLineWidthUnit, 0.3f, {-hx, y0, 0, 1}, {-hx, y1, 0, 1}, c.data());
      if (g.edge_right[size_t(i)]) emit_line(out, kLineWidthUnit, 0.3f, {hx, y0, 0, 1}, {hx, y1, 0, 1}, c.data());
    }
  }
}

void arm_end(Game& g, int reason) {
  if (!in_game(g) || 0.0f < g.end_timer) return;
  g.end_timer = 1.5f;
  g.end_reason = reason;
  const LifeRecord& p = g.recs[0];
  bool hint = false;
  if (g.multiplayer)
    hint = false;
  else if (g.rules.mode == 5)
    hint = !p.bomb_used && !p.shield_hit && g.world->records[0].zones >= 10;
  else if (g.rules.mode == 3)
    hint = !p.bomb_used && !p.shield_hit && g.waves.walls >= 4;
  else if (g.rules.mode == 0)
    hint = !(g.achieved & 0x200) && 0.0f < g.world->bounce_hint;
  else if (g.rules.mode == 4)
    hint = (!(g.achieved & 0x400) && g.edge_count >= 25) || (!(g.achieved & 0x800) && g.edge_count >= 25);
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
  g.world->multiplayer = false;
  g.world->touch = [&g](const Touch& t) { player_touched(g, t); };
  wall_quads(kArenaHalfX, kArenaHalfY, g.grid_tuning.depth, kWallOuterScale, g.walls);
  // No player until the first spawn: the slot record starts zeroed (no
  // wait, spawn point at the centre), so it spawns at the end of the first
  // step. Lives and bombs come from the mode (sub_82032FF8).
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
  g.world->scored = [&g](int r, size_t k) { score_awards(g, r, k); };
  g.world->achieve = [&g](uint32_t bit, int flags) { award_achievement(g, bit, flags); };
  g.world->geom_collected = [&g](int v) { g.sequence.geom_collected(v); };
  g.sequence.achieve = [&g](uint32_t bit, int flags) { award_achievement(g, bit, flags); };
  // The player units' looks: tint and glowColour rgb times alpha (fmuls;
  // the glow's red as a * r), glowSize, the model.
  static const char* const kLooks[5] = {"player", "player1", "player2", "player3", "player4"};
  for (size_t i = 0; i < g.looks.size(); ++i) {
    const auto u = i == 0 ? unit : find_unit(*g.db, kLooks[i]);
    if (!u) continue;
    ShipLook& look = g.looks[i];
    if (const FlaskStruct* tint = as_struct(u->s->find("tint"))) {
      const float a = as_float(tint->find("a"));
      look.tint[0] = as_float(tint->find("r")) * a;
      look.tint[1] = as_float(tint->find("g")) * a;
      look.tint[2] = as_float(tint->find("b")) * a;
      look.tint[3] = 1.0f;
    }
    if (const FlaskStruct* glow = as_struct(u->s->find("glowColour"))) {
      const float a = as_float(glow->find("a"));
      look.glow[0] = a * as_float(glow->find("r"));
      look.glow[1] = as_float(glow->find("g")) * a;
      look.glow[2] = as_float(glow->find("b")) * a;
    }
    look.glow_size = as_float(u->s->find("glowSize"));
    look.unit = g.units->find(kLooks[i]);
    const FlaskValue* model_ref = u->s->find("model");
    const auto* ref = model_ref ? std::get_if<FlaskRef>(&model_ref->v) : nullptr;
    if (!ref) {
      if (i > 0) continue;
      SDL_Log("player has no model");
      return false;
    }
    const FlaskValue model = g.db->decode(ref->record);
    if (const FlaskStruct* m = as_struct(&model)) look.model = build_model_geometry(*m, *g.gm);
  }
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
  // The achievements from the SPA resource (XDBF at 0x821FB480).
  try {
    const auto image = read_file(cache / "xex" / "image.bin");
    const size_t at = 0x821FB480 - 0x82000000;
    if (at < image.size()) {
      const Xdbf x = Xdbf::parse(std::span<const uint8_t>(image).subspan(at));
      // sub_820802E0: achievement id to bit.
      static const std::map<int, uint32_t> kBits = {{5, 0x001}, {7, 0x002}, {11, 0x004}, {12, 0x008},
                                                    {1, 0x010}, {3, 0x020}, {6, 0x040}, {2, 0x080},
                                                    {4, 0x100}, {8, 0x200}, {9, 0x400}, {10, 0x800}};
      auto latin1 = [](const std::string& u) {
        // UTF-8 to the font's single bytes (Latin-1; others '?').
        std::string out;
        for (size_t i = 0; i < u.size();) {
          const unsigned char c = static_cast<unsigned char>(u[i]);
          if (c < 0x80) {
            out.push_back(char(c));
            ++i;
          } else if ((c & 0xE0) == 0xC0 && i + 1 < u.size()) {
            const unsigned v = (c & 0x1Fu) << 6 | (static_cast<unsigned char>(u[i + 1]) & 0x3Fu);
            out.push_back(v <= 0xFF ? char(v) : '?');
            i += 2;
          } else {
            out.push_back('?');
            ++i;
            while (i < u.size() && (static_cast<unsigned char>(u[i]) & 0xC0) == 0x80) ++i;
          }
        }
        return out;
      };
      for (const Xdbf::Achievement& a : x.achievements()) {
        Game::AchievementInfo info;
        const auto b = kBits.find(a.id);
        info.bit = b == kBits.end() ? 0 : b->second;
        info.name = latin1(x.string(1, a.name));
        info.description = latin1(x.string(1, a.description));
        info.gamerscore = a.gamerscore;
        if (auto icon = decode_png(x.image(a.image))) info.icon = std::move(*icon);
        g.achievements.push_back(std::move(info));
      }
    }
  } catch (const std::exception&) {
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
  // sub_8204EDD8: Pacifism's edges cleared.
  g.edge_count = 0;
  g.edge_top.fill(false);
  g.edge_bottom.fill(false);
  g.edge_left.fill(false);
  g.edge_right.fill(false);
  // The single-player slot: pad 0 moves and fires, record 0, colour 0,
  // the unit "player".
  g.multiplayer = false;
  g.versus = false;
  g.ship_count = 1;
  g.record_count = 1;
  g.ships.fill(Ship{});
  g.player_list.clear();
  // sub_82032FF8: lives and bombs into the records.
  g.recs.fill(LifeRecord{});
  for (int r = 0; r < g.record_count; ++r) {
    g.recs[size_t(r)].lives = g.rules.start_lives;
    g.recs[size_t(r)].bombs = g.rules.start_bombs;
  }
  g.bullets.clear();
  // sub_82036250: the object lists emptied, the arena and the grid reset,
  // the spawners reset (sub_8203E538: Deadline's, the shared one, then
  // Sequence's), Pacifism's edges cleared.
  w.new_game();
  w.multiplayer = g.multiplayer;
  w.players.assign(size_t(g.ship_count), PlayerTarget{});
  for (int k = 0; k < g.ship_count; ++k) {
    PlayerTarget& t = w.players[size_t(k)];
    t.radius = 5.0f;
    t.removed = true;
    t.record = g.ships[size_t(k)].record;
    t.colour = g.ships[size_t(k)].colour;
  }
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
  g.end_sounded = false;
  g.flow = 2;
  if (g.music) g.music(true);
}

GameAudioState audio_state(const Game& g) {
  GameAudioState s;
  const World& w = *g.world;
  s.paused = frozen(g);
  const Ship& sh = g.ships[0];
  const PlayerMotion& m = sh.motion;
  const bool alive = sh.life.alive();
  // The smallest distance from a live player to a zone's edge (King).
  for (const auto& e : w.entities) {
    if (e->type != 0x1C || (e->flags & kEntityRemoved) || !alive) continue;
    const float dx = e->x - m.x, dy = e->y - m.y;
    const float d = std::sqrt(dx * dx + dy * dy) - e->radius;
    const float m = d > 0.0f ? d : 0.0f;
    if (m < s.distance_from_hill) s.distance_from_hill = m;
  }
  if (alive) {
    s.thrust = m.thrust_level;
    s.player_x = m.x;
    s.player_y = m.y;
  }
  s.progression_level = g.sequence.shown_level;
  s.multiplier = w.records[0].multiplier;
  s.score = double(w.records[0].score);
  s.no_enemies = w.enemy_count;
  auto remaining = [](int limit, int elapsed) { return std::clamp(limit - elapsed, 0, limit); };
  s.time_left = remaining(int(g.rules.length), int(g.elapsed));
  s.level_time_left = remaining(int(g.rules.level_time), int(g.sequence.level_timer));
  s.bomb_kills = w.bomb_kills;
  return s;
}

void award_achievement(Game& g, uint32_t bit, int flags) {
  if (g.multiplayer || (g.earned & bit)) return;
  g.earned |= bit;
  if (g.achieved & bit) return;
  if (!(flags & 1)) g.world->play(audio::Event::kHiScoreAchieved);
  if (!(flags & 2)) {
    g.achieved |= bit;
    if (g.unlocked) g.unlocked(bit);
  }
}

void delete_objects(Game& g) {
  g.world->delete_objects();
  g.bullets.clear();
  for (Ship& s : g.ships) {
    s.life.present = false;
    s.life.removed = true;
  }
  g.player_list.clear();
  g.field_live = false;
}

void ScoreTable::insert(int64_t score, int multiplier, const std::string& name) {
  entry[0] = {score, multiplier, name};
  if (entry[1].score < score) entry[1] = entry[0];
  if (score > 0) {
    for (size_t i = 9; i > 2; --i) entry[i] = entry[i - 1];
    entry[2] = entry[0];
  }
}

void step(Game& g, std::span<const Sticks> pads) {
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
    in.alive = any_alive(g);
    in.over = g.over;
    in.after_death = &g.after_death;
    in.respawn_wait = &g.ships[0].life.respawn_wait;
    in.level_time = g.rules.level_time;
    in.transition = g.transition_switch;
    g.sequence.step(w, in);
  } else if (any_alive(g)) {
    if (g.rules.mode == 0)
      g.spawner.step(w, g.elapsed);
    else
      g.waves.step(w, g.rules.mode, g.recs[0].bomb_used || g.recs[0].shield_hit, g.after_death);
  }
  const int second = int(g.elapsed);
  g.elapsed = kStep + g.elapsed;
  w.clock = g.elapsed;
  if (g.rules.mode == 0 && in_game(g) && second != int(g.elapsed)) w.play(audio::Event::kDeadlineTickClock);
  update_scoring(g);
  // Last step's kill markers go (sub_82043368).
  w.death_markers.clear();

  // The player list in creation order.
  for (const size_t k : std::vector<size_t>(g.player_list))
    if (g.ships[k].life.alive()) update_player(g, k, pads);
  for (int k = 0; k < g.ship_count; ++k) {
    const Ship& sh = g.ships[size_t(k)];
    PlayerTarget& target = w.players[size_t(k)];
    target.x = sh.motion.x;
    target.y = sh.motion.y;
    target.vx = sh.motion.vx;
    target.vy = sh.motion.vy;
    target.fading = sh.life.fading;
    target.push_vx = target.push_vy = 0.0f;
  }
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
  for (int r = 0; r < g.record_count; ++r) {
    LifeRecord& rec = g.recs[size_t(r)];
    rec.lives_pulse = rec.lives_pulse * 0.9f;
    rec.bombs_pulse = rec.bombs_pulse * 0.9f;
  }
  // A player that has just started to fade in bursts into view (single
  // player only).
  if (!g.multiplayer) {
    const PlayerLife& p = g.ships[0].life;
    const bool fading = p.alive() && p.fading;
    if (fading && !g.was_fading) spawn_burst(g, g.ships[0]);
    g.was_fading = fading;
  }
  // The camera follows after the entity updates (sub_820366A0): the
  // player object while it exists, else the slot's spawn point.
  // Out of a game the view drifts back to the centre.
  if (!in_game(g)) {
    camera_front_end(g.camera, g.camera_idle, kStep);
  } else {
    Ship& sh = g.ships[0];
    if (sh.life.present)
      camera_follow_single(g.camera, &sh.motion.x, &sh.motion.y, 1, kArenaHalfX, kArenaHalfY);
    else
      camera_follow_single(g.camera, &sh.life.spawn_x, &sh.life.spawn_y, 1, kArenaHalfX, kArenaHalfY);
    g.camera_idle = 0.0f;
  }
  // No player left since one spawned: the field is cleared; then a slot
  // without a player may spawn one.
  if (g.field_live && !any_alive(g)) {
    w.play(audio::Event::kPlayerDieMusic);
    clear_field(g);
    g.field_live = false;
  }
  // Respawns and the game over, while a game runs and is not ending.
  if (in_game(g) && !ending(g)) {
    respawn(g);
    update_scoring(g);
    // Game over: with lives, when no player is left and none can come
    // back (live players + slots with a free spawn + the records' lives <
    // 1); with the time limit, when time is up; or when Sequence says so
    // (dat_8219CE71). Sounds 1 and 0x25; unless Sequence holds the end back
    // (dat_8219CE72) the field is cleared and the end counts down
    // (sub_820364D0, sub_82034408(2)).
    bool end = g.sequence.force_over;
    if (g.rules.lives_mode == 0) {
      int left = 0;
      for (const size_t k : g.player_list) left += g.ships[k].life.alive() ? 1 : 0;
      for (int k = 0; k < g.ship_count; ++k) left += g.ships[size_t(k)].life.free_spawn ? 1 : 0;
      for (int r = 0; r < g.record_count; ++r) left += g.recs[size_t(r)].lives;
      end = end || left < 1;
    }
    else if (g.rules.lives_mode == 2)
      end = end || g.rules.length <= w.clock;
    if (end) {
      g.over = true;
      if (!g.end_sounded) {
        g.end_sounded = true;
        w.play(audio::Event::kGameOver);
        w.play(audio::Event::kStopInGameMusic);
      }
      if (g.sequence.allow_end) {
        clear_field(g);
        arm_end(g, 2);
      }
    }
  }

  // The Rebound hint fades (0x82037ABC), then Pacifism's edge tracker
  // (sub_8204FAD8).
  {
    const float t = w.bounce_hint - kStep;
    w.bounce_hint = t >= 0.0f ? t : 0.0f;
  }
  if (!g.multiplayer && g.rules.mode == 4 && (g.achieved & 0xC00) != 0xC00) {
    if (g.ships[0].life.alive()) track_edges(g);
    if (g.edge_count >= 100) award_achievement(g, 0x400, 0);
    if (g.edge_count >= 200) award_achievement(g, 0x800, 0);
  }

  // The grid (sub_8205C8F8, then sub_8205B880 on worker threads): pushes
  // gathered from the entities, the spring step, and the line pass from
  // the positions the step read. Bullets push with radius +0x8C and
  // strength 2.0 (sub_8205B578).
  GridInputs in;
  // Active wells light the grid (sub_8205B578).
  well_grid_lights(w, in.lights);
  // The specials: a fading-in player pulls, the first blast pushes.
  for (const size_t k : g.player_list)
    if (g.ships[k].life.alive() && g.ships[k].life.fading) {
      in.fading_player = Vec2{g.ships[k].motion.x, g.ships[k].motion.y};
      break;
    }
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
  for (const size_t k : g.player_list) {
    const Ship& sh = g.ships[k];
    const ShipLook& look = g.looks[size_t(sh.colour_look)];
    if (sh.life.alive()) emit_light(g.lights, look.glow_size, sh.motion.x, sh.motion.y, look.glow);
  }

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

  // What the wells' pulls and shoves did to the players' velocity.
  for (int k = 0; k < g.ship_count; ++k) {
    PlayerMotion& m = g.ships[size_t(k)].motion;
    m.vx = m.vx + w.players[size_t(k)].push_vx;
    m.vy = m.vy + w.players[size_t(k)].push_vy;
  }

  // Entities removed this step are deleted, the player objects with them.
  w.purge();
  for (int k = 0; k < g.ship_count; ++k)
    if (g.ships[size_t(k)].life.removed) g.ships[size_t(k)].life.present = false;
  std::erase_if(g.player_list, [&g](size_t k) { return !g.ships[k].life.present; });

  // The achievements checked every step of a game (0x82037B0C..):
  // Treaty (King: 30 zones, no bomb, no shield hit), Surf (Waves: 8 rocket
  // walls likewise), Phobia (Waves: 1000 points at multiplier 1), Rebound
  // (Deadline: 75 bounced-shot kills), Magpie (multiplier 500).
  if (in_game(g) && !g.multiplayer) {
    const LifeRecord& p = g.recs[0];
    const ScoreRecord& rec = w.records[0];
    if (g.rules.mode == 5) {
      if (!p.bomb_used && !p.shield_hit && rec.zones > 29) award_achievement(g, 0x002, 0);
    } else if (g.rules.mode == 3) {
      if (!p.bomb_used && !p.shield_hit && g.waves.walls > 7) award_achievement(g, 0x040, 0);
      if (rec.score > 999 && rec.multiplier == 1) award_achievement(g, 0x100, 0);
    } else if (g.rules.mode == 0 && w.bounce_kills > 0x4A) {
      award_achievement(g, 0x200, 0);
    }
    if (rec.multiplier > 499) award_achievement(g, 0x080, 0);
  }
}

void draw_game_hud(const Game& g, LineBatch& ui) {
  // The high score: the session's best for the mode, or this game's score
  // when higher.
  HudState hud;
  if (g.rules.mode >= 0 && g.rules.mode < 6) hud.high_score = g.tables[size_t(g.rules.mode)].entry[1].score;
  const LifeRecord& p = g.recs[0];
  hud.score = g.world->records[0].score;
  hud.multiplier = g.world->records[0].multiplier;
  hud.bombs = p.bombs;
  hud.lives = p.lives;
  hud.lives_pulse = p.lives_pulse;
  hud.bombs_pulse = p.bombs_pulse;
  hud.start_lives = g.rules.start_lives;
  hud.mode = g.rules.mode;
  hud.level_time = g.rules.level_time;
  hud.level_timer = g.sequence.level_timer;
  hud.clock = g.elapsed;
  hud.length = g.rules.length;
  hud.lives_mode = g.rules.lives_mode;
  draw_hud(g.font, g.hud_text, hud, ui);
  // The achievement hints under the field (single player, not paused):
  // King's zones while no bomb or shield hit (from 10, text 0xA3 / 0xA4)
  // and Waves' rocket walls likewise (from 4, 0xA5 / 0xA6).
  if (frozen(g) || g.multiplayer) return;
  const float* normal = UiPalette::defaults()[kUiNormal].data();
  std::string hint;
  if (g.rules.mode == 5 && !p.bomb_used && !p.shield_hit && g.world->records[0].zones >= 10) {
    const int n = g.world->records[0].zones;
    hint = n < 30 ? format("%s - %i / %i", g.text(0xA3).c_str(), n, 30) : format("%s - %i", g.text(0xA4).c_str(), n);
  } else if (g.rules.mode == 3 && !p.bomb_used && !p.shield_hit && g.waves.walls > 3) {
    const int n = g.waves.walls;
    hint = n < 8 ? format("%s - %i / %i", g.text(0xA5).c_str(), n, 8) : format("%s - %i", g.text(0xA6).c_str(), n);
  }
  Rgba colour{normal[0], normal[1], normal[2], normal[3]};
  if (g.rules.mode == 0 && !(g.achieved & 0x200) && 0.0f < g.world->bounce_hint) {
    // Rebound, fading over its last second.
    const float a = std::clamp(g.world->bounce_hint, 0.0f, 1.0f);
    colour = {normal[0] * a, normal[1] * a, normal[2] * a, normal[3]};
    hint = format("%s - %i / %i", g.text(0xA7).c_str(), std::clamp(g.world->bounce_kills, 0, 75), 75);
  } else if (g.rules.mode == 4 && g.edge_count >= 25) {
    // Wax On, then Wax Off, as a percentage.
    if (!(g.achieved & 0x400))
      hint = format("%s - %i%%", g.text(0xA8).c_str(),
                    std::clamp(int((float(g.edge_count) / float(100)) * 100.0f), 0, 100));
    else if (!(g.achieved & 0x800))
      hint = format("%s - %i%%", g.text(0xA9).c_str(),
                    std::clamp(int((float(g.edge_count) / float(200)) * 100.0f), 0, 100));
  }
  if (!hint.empty()) g.font.centre(ui, 320.0f, 334.0f, 8.0f, 8.0f, colour.data(), hint);  // dat_82004994
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
void tick(Game& g, std::span<const Sticks> pads, Renderer& r, SDL_GPUCommandBuffer* cmd) {
  step(g, pads);
  r.update_light_map(cmd, g.warp_mesh, g.lights);
}

}  // namespace geow2

#pragma once

#include <cstdint>

#include "game/player_motion.h"

namespace geow2 {

// A mode's player rules, as its setup function leaves them on top of the
// reset (sub_820348E8). The defaults are Deadline's (sub_82034EA0).
struct ModeRules {
  int mode = 0;                 // dat_8219D254: 0 Deadline, 1 Evolved, 2 Sequence, 3 Waves, 4 Pacifism, 5 King
  int lives_mode = 2;           // dat_8219D24C: 0 lives, 2 the time limit
  int start_lives = 0;          // dat_8219D264
  int start_bombs = 3;          // dat_8219D268
  // dat_8219D26C: bit 0 no extra lives from score, bit 1 no extra bombs
  // (and no bombing without a starting count).
  uint32_t flags = 1;
  float respawn_delay = 1.0f;   // dat_8219D270 (the reset sub_820348E8, 0x82034B08)
  float length = 180.0f;        // dat_8219D258 (dat_82000CD8)
  float level_time = 0.0f;      // dat_8219D25C (Sequence)
};

// The single-player setups: sub_82034EA0 (Deadline), sub_82034F38
// (Evolved), sub_82034FA8 (Sequence), sub_82035030 (Waves), sub_820350B0
// (Pacifism), sub_82035130 (King).
ModeRules mode_rules(int mode);

// A ship's life outside its movement: the player object's shield and bomb
// state and its slot record (0x8219CEB4 + 0x6C per slot).
struct PlayerLife {
  // The object (sub_82051448). `present` is the slot's object pointer
  // (dat_8219CEB4), cleared when the removed object is deleted.
  bool present = false;
  bool removed = false;          // flag 1
  bool fading = false;           // flag 0x10: fading in, no update
  float fade = 0.0f;             // +0x38
  bool bomb_latch = false;       // +0x84
  float bomb_wait = 0.0f;        // +0x88
  float shield = 0.0f;           // +0x8C
  bool shield_shown = false;     // +0x90: model part 1 drawn
  // The slot record.
  float respawn_wait = 0.25f;    // dat_8219CEB8 (dat_82000720 at the reset)
  float spawn_x = 0.0f, spawn_y = 0.0f;  // dat_8219CEC0 / dat_8219CEC4
  bool free_spawn = true;        // dat_8219CEE0: the next spawn costs no life
  int wall_wait = 0;             // +0x94: the wall sound's hold-off (sub_8204E908)
  bool alive() const { return present && !removed; }
};

// A score record's lives and bombs (0x8219D068 + 0x78 per record; the
// score, multiplier and kills are World::records).
struct LifeRecord {
  int lives = 0;                 // +0x30 (dat_8219D098)
  int bombs = 0;                 // +0x34 (dat_8219D09C)
  int deaths = 0;                // +0x3C (dat_8219D0A4)
  int shield_kills = 0;          // +0x68 (dat_8219D0D0)
  int64_t bomb_at = 100000;      // +0x48 (dat_8219D0B0): the next extra bomb's score
  int64_t life_at = 100000;      // +0x50 (dat_8219D0B8): the next extra life's score
  float lives_pulse = 0.0f;      // +0x58 (dat_8219D0C0): the HUD's lives row
  float bombs_pulse = 0.0f;      // +0x5C (dat_8219D0C4): the HUD's bombs row
  bool bomb_used = false;        // +0x60 (dat_8219D0C8)
  bool shield_hit = false;       // +0x61 (dat_8219D0C9)
  int rank = 0;                  // +0x6C (dat_8219D0D4): 0 first
  bool tie = true;               // +0x70 (dat_8219D0D8)
};

// sub_82037EA0 axes 8 and 9: a trigger's raw byte to [0, 1].
float trigger_axis(uint8_t raw);

// The new object's state (sub_82051448): fading in from 0 with a shield of
// 3.33 seconds (dat_82001E94), shown.
void player_spawned(PlayerLife& p);

// The end of the player update (sub_8204FF00): the shield and the bomb
// wait count down; the shield blinks over its last second. Returns true on
// the blink frames that sound event 8 (ShieldOff) marks.
bool player_timers(PlayerLife& p, float step);

// sub_8204F288: `trigger` is the larger trigger value. Returns 1 when a
// bomb goes off (a blast of 400 at 533.2 a second from the player, and
// sub_8203E4B0; sound event 6), 2 when pressed without bombs (0x35), else
// 0.
int player_bomb(PlayerLife& p, LifeRecord& r, float trigger, const ModeRules& rules);

class World;

// The exhaust (sub_8204FF00, 0x82050418..0x82050A80), while the ship
// thrusts: four particles from 7 behind the ship (after this step's move),
// flung backward with its speed (Unit.Player.ParticleThrust) plus a random
// kick, coloured between the colour wheel at the ship's position (turned
// by pi) and 1.1 times it; more and whiter while boosted (+0xCC), paler
// while slowed (+0xD8).
void player_exhaust(World& w, const PlayerMotion& p);

}  // namespace geow2

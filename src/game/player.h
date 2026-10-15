#pragma once

#include <cstdint>

namespace geow2 {

// A mode's player rules, as its setup function leaves them. Deadline's is
// sub_82034EA0.
struct ModeRules {
  int mode = 0;                 // dat_8219D254 (0 Deadline)
  int lives_mode = 2;           // dat_8219D24C: non-zero respawns without lives
  int start_lives = 0;          // dat_8219D264
  int start_bombs = 3;          // dat_8219D268
  uint32_t flags = 1;           // dat_8219D26C (bit 2: bombs need a count)
  float respawn_delay = 0.0f;   // dat_8219D270
  float length = 180.0f;        // dat_8219D258 (dat_82000CD8)
};

// A player's life outside its movement: the object's shield and bomb
// state, its slot record (0x8219CEA8 + 0x6C per slot) and its score
// record (0x8219D068 + 0x78 per player).
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
  float respawn_wait = 0.0f;     // dat_8219CEB8
  float spawn_x = 0.0f, spawn_y = 0.0f;  // dat_8219CEC0 / dat_8219CEC4
  bool free_spawn = false;       // dat_8219CEE0: the next spawn costs no life
  // The score record.
  int lives = 0;                 // dat_8219D098
  int bombs = 0;                 // dat_8219D09C
  int deaths = 0;                // dat_8219D0A4
  int shield_kills = 0;          // dat_8219D0D0

  bool alive() const { return present && !removed; }
};

// sub_82037EA0 axes 8 and 9: a trigger's raw byte to [0, 1].
float trigger_axis(uint8_t raw);

// The new object's state (sub_82051448): fading in from 0 with a shield of
// 3.33 seconds (dat_82001E94), shown.
void player_spawned(PlayerLife& p);

// The end of the player update (sub_8204FF00): the shield and the bomb
// wait count down; the shield blinks over its last second (sound event 8
// on each blink is for the sound system).
void player_timers(PlayerLife& p, float step);

// sub_8204F288: `trigger` is the larger trigger value. Returns true when a
// bomb goes off (a blast of 400 at 533.2 a second from the player, and
// sub_8203E4B0); sound events 6 and 0x35 are for the sound system.
bool player_bomb(PlayerLife& p, float trigger, const ModeRules& rules);

}  // namespace geow2

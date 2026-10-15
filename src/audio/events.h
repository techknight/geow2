#pragma once

#include <cstdint>

namespace geow2::audio {

// The sound events the game fires (sub_820787F8). The ids are the
// original's; their names are looked up in the game's own table
// (dat_8218D588) to find each object's ActionIn of that name. The comment
// is the original call site and its condition.
enum class Event : uint8_t {
  kGameStart = 0x00,              // sub_8207A048 once at audio init (after 0x26)
  kGameOver = 0x01,               // sub_820366A0 game end, once per game (latch dat_8219CE74)
  kEnemyExplode = 0x02,           // sub_82046010 generic kill (flags & 4 clear); sub_8204C958 mayfly; sub_820556E8
                                  // sentry
  kPlayerSpawn = 0x03,            // sub_820366A0 respawn, fewer than 2 players
  kPlayerDie = 0x04,              // sub_8204E988 player slot 9, death
  kFireNormal = 0x05,             // sub_8204EFD0 volley, weapon timer +0xD0 <= 0 and +0xA0 < 1
  kFireSmartbomb = 0x06,          // sub_8204F288 bomb with bombs > 0; sub_8204B908 power-up type 5
  kHitWall = 0x07,                // sub_8204E908 wall contact while +0x94 < 1; sub_82054218 repulsar touch (35 ms
                                  // gate dat_8219FACC)
  kShieldOff = 0x08,              // sub_8204FF00 0 < shield t < 1.0 and int(60 t) mod 10 == 4
  kPlayerShieldedHitEnemy = 0x09, // sub_8204EEA8 touched with shield +0x8C > 0
  kWandererSpawn = 0x0A,          // sub_8203BEE8 spawn type 1
  kGruntSpawn = 0x0B,             // sub_8203BEE8 type 2
  kSpinnerSpawn = 0x0C,           // sub_8203BEE8 type 4
  kWeaverSpawn = 0x0D,            // sub_8203BEE8 type 3
  kGravityWellSpawn = 0x0E,       // sub_8203BEE8 type 5
  kGravityWellHit = 0x0F,         // sub_82048570
  kGravityWellDie = 0x10,         // sub_820497C0 unless +0x85
  kGravityWellExplode = 0x11,     // sub_82049370
  kGravityWellWarning = 0x12,     // sub_82049370 hits == MaxHits, +0x85 == 0, +0x88 % 30 == 0
  kRepulsarSpawn = 0x13,          // sub_8203BEE8 type 6
  kStartRepulsarActive = 0x14,    // sub_82053DF8 charge timer +0x90 > +0x94
  kSnakeSpawn = 0x15,             // sub_8203BEE8 type 8
  kUiChange = 0x16,               // menus sub_82090180, 82084588, 8208D800, 8208D918, 8208AB80; leaderboard 82089650
  kUiDelete = 0x17,               // menus sub_82090180, 8207A658, 8208AB80 (B / back)
  kUiConfirm = 0x18,              // menus (A/Start), option toggles 82086CD0..86EC8, volume steps 82087258/82087358,
                                  // pause accept 820336D8
  kPickupSmartbomb = 0x19,        // sub_820408D0 bomb score threshold D0B0
  kPickupExtralife = 0x1A,        // sub_820408D0 life score threshold D0B8
  kMultiplierIncreased = 0x1B,    // never fired by game code
  kStartUiMusic = 0x1C,           // sub_82078A50(0): intro end 8207E9F0, back to menu 820338D8
  kStartSong1 = 0x1D,             // sub_82078A50(1) mode 0 (Deadline), 6, 7
  kStartSong2 = 0x1E,             // sub_82078A50(1) mode 1 (Evolved)
  kStartSong3 = 0x1F,             // sub_82078A50(1) mode 2 (Sequence)
  kStartSong4 = 0x20,             // sub_82078A50(1) mode 4 (Pacifism)
  kStartSong5 = 0x21,             // sub_82078A50(1) mode 3 (Waves)
  kStartSong6 = 0x22,             // sub_82078A50(1) mode 5 (King)
  kBulletHitWall = 0x23,          // sub_82051AF0 first wall contact of a shot (tail call)
  kHiScoreAchieved = 0x24,        // sub_82034CA0 achievement newly unlocked
  kStopInGameMusic = 0x25,        // sub_820366A0 game end, with 0x01
  kSilence = 0x26,                // sub_8207A048 once at audio init
  kNewSpawnGroup = 0x27,          // sub_8203DED0 Deadline new spawn group
  kNewEnemyType = 0x28,           // sub_8203BEE8 first spawn of a type (latch dat_8219DB74[type])
  kNewHighScore = 0x29,           // never fired by game code
  kGateScore1 = 0x2A,             // sub_820476F0 gate chain 1
  kGateScore2 = 0x2B,             // chain 2
  kGateScore3 = 0x2C,             // chain 3
  kGateScore4 = 0x2D,             // chain 4
  kGateScore5 = 0x2E,             // chain >= 5
  kDuckSpawn = 0x2F,              // sub_8203BEE8 type 22
  kRocketSpawn = 0x30,            // sub_8203BEE8 type 12
  kGateSpawn = 0x31,              // sub_8203BEE8 type 16 (once per pair)
  kGateTrigger = 0x32,            // never fired by game code
  kGateBounceBullet1 = 0x33,      // sub_82051B18 shot bounces off gate beam; alternates with 0x34, 35 ms gate
                                  // dat_8219F968
  kGateBounceBullet2 = 0x34,      // as 0x33 (first call gives 0x34)
  kBombDryFire = 0x35,            // sub_8204F288 bomb pressed with 0 bombs, cooldown +0x88 <= 0
  kKingCircleSpawn = 0x36,        // sub_8204AD38 zone created
  kKingCircleFade = 0x37,         // sub_8204AEE8 zone timer runs out
  kSpawnRocketWave = 0x38,        // sub_8203FFB8 rocket wave
  kRocketFlip = 0x39,             // sub_82054E30
  kDuckMove = 0x3A,               // sub_82044330
  kAmoebaSpawn = 0x3B,            // sub_8203BEE8 type 11
  kSentinelSpawn = 0x3C,          // sub_8203BEE8 type 18
  kSentinelSpit = 0x3D,           // sub_820552C0 sentinel fires
  kPinSpawn = 0x3E,               // sub_8203BEE8 type 20
  kPinRicochet1 = 0x3F,           // sub_82051E58 shot bounce (type codes 0x11/0x1F); alternates with 0x40, 35 ms gate
                                  // dat_8219F970
  kPinRicochet2 = 0x40,           // as 0x3F (first call gives 0x40)
  kBigWaveSoon = 0x41,            // sub_8203ECC0 (4 sites) wave entry scheduled
  kTickClock = 0x42,              // sub_8203A4F0 Sequence state 3, each whole second while the level is live
  kClockStopped = 0x43,           // sub_8203A4F0, sub_82039708 Sequence clock stop (before a result)
  kResultSuccess = 0x44,          // sub_82039708(1)
  kResultDied = 0x45,             // sub_8203A4F0 player died; sub_82039708(2)
  kResultOutOfTime = 0x46,        // sub_8203A4F0 CE6C >= D25C; sub_82039708(3)
  kPickupGeom1 = 0x47,            // sub_8204D118 geom collected; alternates with 0x48, 35 ms gate dat_8219F304
  kPickupGeom2 = 0x48,            // as 0x47 (first call gives 0x48)
  kIntermissionEnter = 0x49,      // sub_82039550 intermission countdown reaches 0
  kFirstEnterHill = 0x4A,         // sub_8204AEE8 player enters a zone
  kDeadlineTickClock = 0x4B,      // sub_820366A0 Deadline, once per second
  kSwooshOut = 0x4C,              // sub_8203A4F0 Sequence state 0 (level 0, timer > 1.4) or state 1 (timer > 4.0 or
                                  // TransitionSwitch > 0.5)
  kSpinnerSplit = 0x4D,           // sub_820588F0
  kSnakeExplode = 0x4E,           // sub_82057CE0
  kSnakeHitTail = 0x4F,           // sub_82056B08 shot hits a segment (tail call)
  kPlayerDieMusic = 0x50,         // sub_820366A0 no player alive (latch CE7D)
  kMultiGameWon = 0x51,           // sub_820366A0 game end, sole survivor won
  kMultiLeaderChanged0 = 0x52,    // sub_820366A0 unique score leader changed (id = 0x52 + slot)
  kMultiLeaderChanged1 = 0x53,    // as 0x52
  kMultiLeaderChanged2 = 0x54,    // as 0x52
  kMultiLeaderChanged3 = 0x55,    // as 0x52
  kMultiPlayerSpawn0 = 0x56,      // sub_820366A0 multiplayer respawn, by slot
  kMultiPlayerSpawn1 = 0x57,      // as 0x56
  kMultiPlayerSpawn2 = 0x58,      // as 0x56
  kMultiPlayerSpawn3 = 0x59,      // as 0x56
  kMultiLastManStanding = 0x5A,   // sub_820366A0 one player left, not the leader; once (latch CE7C)
  kMultiFire0 = 0x5B,             // sub_8204EFD0 volley in multiplayer (+0xA0 >= 1), by slot
  kMultiFire1 = 0x5C,             // as 0x5B
  kMultiFire2 = 0x5D,             // as 0x5B
  kMultiFire3 = 0x5E,             // as 0x5B
  kProtonSpawn = 0x5F,            // sub_8203BEE8 type 14
  kSpawnPowerUp = 0x60,           // sub_8203E5C8 power-up spawned
  kPowerUpSpeed = 0x61,           // sub_8204B908 type 1
  kPowerUpShield = 0x62,          // type 2
  kPowerUpSlow = 0x63,            // type 3
  kPowerUpMagnet = 0x64,          // type 4
  kPowerUpWeapon = 0x65,          // type 6
  kPowerUpFreeze = 0x66,          // type 7
  kPowerUpEndSpeed = 0x67,        // sub_8204FF00 timer +0xCC runs out (shared 35 ms gate dat_8219F960)
  kPowerUpEndSlow = 0x68,         // timer +0xD8 runs out (same gate)
  kPowerUpEndMagnet = 0x69,       // timer +0xDC runs out (same gate)
  kPowerUpEndWeapon = 0x6A,       // timer +0xD0 runs out (same gate)
  kPowerUpEndFreeze = 0x6B,       // timer +0xE0 runs out (same gate)
  kFireSpecial = 0x6C,            // sub_8204EFD0 volley with weapon timer +0xD0 > 0
  kWaxOnClick = 0x6D,             // sub_8204F7C8 Pacifism edge flag crossed
  kWaxOffClick = 0x6E,            // sub_8204F7C8 edge flag cleared
  kUnlockedNewMode = 0x6F,        // sub_8208ED00 mode unlocked
  kBong = 0x70,                   // sub_8207E9F0 intro phase 1 timer > 5.0
};
inline constexpr int kEventCount = 0x71;

// The parameters the game sets (sub_820783A8), names in dat_8218D918.
enum class Param : uint8_t {
  kThrust = 0,                  // mean stick magnitude 0..1 of live players
  kPaused = 1,                  // 1.0 while paused
  kMayflyNumbers = 2,           // mayfly count
  kGravityWellPitch0 = 3,       // 3..7: gravity-well hum slots (GameAudio)
  kGravityWellPitch1 = 4,
  kGravityWellPitch2 = 5,
  kGravityWellPitch3 = 6,
  kGravityWellPitch4 = 7,
  kSfxVolume = 8,               // option value squared
  kMusicVolume = 9,             // option value squared
  kMultiplier = 10,
  kScore = 11,
  kNoEnemies = 12,
  kTime = 13,                   // remaining whole seconds
  kDistanceFromHill = 14,
  kProgressionLevelNumber = 15,
  kSongId = 16,                 // set by the music selection only
  kPlayerPosX = 17,
  kPlayerPosY = 18,
  kLevelTime = 19,
  kBombKillCount = 20,
  kMagnetActive = 21,
};
inline constexpr int kParamCount = 0x16;

}  // namespace geow2::audio

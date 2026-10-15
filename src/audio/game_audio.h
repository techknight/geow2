#pragma once

#include <array>
#include <cstdint>

#include "audio/events.h"

namespace geow2::audio {

// What the game-side glue drives: the original's sub_820787F8 (fire an
// event), sub_820783A8 (set a parameter) and the ObjectManager Suspend of
// the six song objects. AudioEngine implements it.
class AudioSink {
 public:
  virtual ~AudioSink() = default;
  virtual void fire(Event e) = 0;
  virtual void set_parameter(Param p, float value) = 0;
  virtual void suspend_songs(bool suspended) = 0;
};

// The 35 ms rate gate in front of some events: the call goes through when
// (now - last) > 34 ms, unsigned, with now from KeTimeStampBundle+0x10
// (sub_82094B50); `last` starts at 0.
class EventGate {
 public:
  bool pass(uint32_t now_ms) {
    if (now_ms - last_ <= 0x22u) return false;
    last_ = now_ms;
    return true;
  }

 private:
  uint32_t last_ = 0;
};

// The values the original feeds every frame (sub_82079BC0), computed by the
// game from its own state.
struct AudioFrame {
  bool paused = false;              // sub_82034588
  float sfx_option = 1.0f;          // the options' volume steps, 0..1
  float music_option = 1.0f;
  bool music_muted = false;         // Audio.MuteMusic (the user's own soundtrack in the original)
  int mayflies = 0;                 // dat_8219F2FC
  float distance_from_hill = 1000.0f;  // min over zones and live players of max(dist - radius, 0), else 1000
  float thrust = 0.0f;              // mean stick magnitude (+0xB0) of live players, else 0
  bool magnet_active = false;       // a live player has +0xDC > 0
  float player_x = 0.0f, player_y = 0.0f;  // first live player, else 0
  int progression_level = 0;        // sub_820392F0
  int multiplier = 0;               // max over players
  double score = 0.0;               // max over players (s64)
  int no_enemies = 0;               // dat_8219E828
  int time_left = 0;                // remaining_seconds(D258, CE68)
  int level_time_left = 0;          // remaining_seconds(D25C, CE6C)
  int bomb_kill_count = 0;          // dat_8219D284
};

// clamp(limit - elapsed, 0, limit) on whole seconds, as sub_82079BC0 does
// for the Time and LevelTime parameters.
int remaining_seconds(int limit, int elapsed);

// The game-side glue of the original's audio calls: rate gates and
// alternating pairs at the call sites, the gravity-well hum slots, the
// per-frame parameter feed, the pause edge and the music selection.
class GameAudio {
 public:
  explicit GameAudio(AudioSink& sink) : sink_(sink) {}

  // sub_8207A048 after the objects are loaded: clear the well slots, fire
  // Silence then Game_start.
  void start();

  // An event without a gate.
  void fire(Event e) { sink_.fire(e); }

  // Geom collected (sub_8204D118): Pickup_geom1/2 alternating, the first
  // call giving Pickup_geom2, behind the gate dat_8219F304.
  void pickup_geom(uint32_t now_ms) { alternate(Event::kPickupGeom1, geom_flag_, geom_gate_, now_ms); }
  // Shot bounced off a gate beam (sub_82051B18): GateBounceBullet1/2, gate dat_8219F968.
  void gate_bounce_bullet(uint32_t now_ms) { alternate(Event::kGateBounceBullet1, gate_flag_, gate_gate_, now_ms); }
  // Shot ricochet off a pin (sub_82051E58): PinRicochet1/2, gate dat_8219F970.
  void pin_ricochet(uint32_t now_ms) { alternate(Event::kPinRicochet1, pin_flag_, pin_gate_, now_ms); }
  // Repulsar touch (sub_82054218): Hit_wall behind the gate dat_8219FACC.
  void repulsar_hit_wall(uint32_t now_ms) {
    if (repulsar_gate_.pass(now_ms)) sink_.fire(Event::kHitWall);
  }
  // A power-up timer running out (sub_8204FF00, sub_8204E830): one gate,
  // dat_8219F960, shared by all five PowerUpEnd events.
  void power_up_end(Event e, uint32_t now_ms) {
    if (power_up_end_gate_.pass(now_ms)) sink_.fire(e);
  }

  // Gravity-well hum (sub_82078610, from the well update): a well (any
  // non-zero key) reports its hits this frame.
  void gravity_well_hits(uintptr_t well, int hits);
  // sub_82078970, each unpaused frame after the wells: free the slots not
  // reported this frame and set Gravity_well_pitch_i = clamp(hits /
  // max_hits, 0, 1).
  void gravity_wells_update(int max_hits);

  // sub_82079BC0: the pause edge, then every per-frame parameter.
  void frame(const AudioFrame& f);

  // sub_82078A50: the menu music (in_game false) or the song of `mode`
  // (dat_8219D254), and SongID. `paused` is the current pause state, for
  // the pause edge the original checks first.
  void select_music(bool in_game, int mode, bool paused);

 private:
  struct WellSlot {
    uintptr_t key = 0;
    int hits = 0;
    bool updated = false;
  };

  void alternate(Event first, bool& flag, EventGate& gate, uint32_t now_ms);
  void pause_edge(bool paused);

  AudioSink& sink_;
  EventGate geom_gate_, gate_gate_, pin_gate_, repulsar_gate_, power_up_end_gate_;
  bool geom_flag_ = false, gate_flag_ = false, pin_flag_ = false;
  std::array<WellSlot, 5> wells_{};  // 0x821D1E38
  bool paused_latch_ = false;        // 0x821D1E74
};

}  // namespace geow2::audio

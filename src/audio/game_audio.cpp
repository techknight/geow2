#include "audio/game_audio.h"

namespace geow2::audio {

int remaining_seconds(int limit, int elapsed) {
  const int left = limit - elapsed;
  if (left < 0) return 0;
  return left <= limit ? left : limit;
}

void GameAudio::start() {
  wells_ = {};
  sink_.fire(Event::kSilence);
  sink_.fire(Event::kGameStart);
}

void GameAudio::alternate(Event first, bool& flag, EventGate& gate, uint32_t now_ms) {
  // (flag == 0) + first: the first call gives the second event.
  if (!gate.pass(now_ms)) return;
  sink_.fire(Event(uint8_t(first) + (flag ? 0 : 1)));
  flag = !flag;
}

void GameAudio::gravity_well_hits(uintptr_t well, int hits) {
  // sub_82078610: the well's own slot, else the first empty slot, else the
  // first slot with fewer hits; otherwise the report is dropped.
  int slot = -1;
  for (int i = 0; i < 5 && slot < 0; ++i)
    if (wells_[i].key == well) slot = i;
  for (int i = 0; i < 5 && slot < 0; ++i)
    if (wells_[i].key == 0) slot = i;
  for (int i = 0; i < 5 && slot < 0; ++i)
    if (wells_[i].hits < hits) slot = i;
  if (slot < 0) return;
  wells_[slot] = {well, hits, true};
}

void GameAudio::gravity_wells_update(int max_hits) {
  for (int i = 0; i < 5; ++i) {
    WellSlot& s = wells_[i];
    if (!s.updated) s.key = 0, s.hits = 0;
    s.updated = false;
    // fsel clamps with 0.0 (dat_82000D30) and 1.0 (dat_82000C78).
    const float x = float(s.hits) / float(max_hits);
    const float lo = -x >= 0.0f ? 0.0f : x;
    const float v = x - 1.0f >= 0.0f ? 1.0f : lo;
    sink_.set_parameter(Param(uint8_t(Param::kGravityWellPitch0) + i), v);
  }
}

void GameAudio::pause_edge(bool paused) {
  // sub_820782E0: Suspend / resume the six songs on each change.
  if (paused == paused_latch_) return;
  paused_latch_ = paused;
  sink_.suspend_songs(paused);
}

void GameAudio::frame(const AudioFrame& f) {
  pause_edge(f.paused);
  sink_.set_parameter(Param::kPaused, f.paused ? 1.0f : 0.0f);
  sink_.set_parameter(Param::kSfxVolume, f.sfx_option * f.sfx_option);
  sink_.set_parameter(Param::kMayflyNumbers, float(f.mayflies));
  sink_.set_parameter(Param::kDistanceFromHill, f.distance_from_hill);
  sink_.set_parameter(Param::kMusicVolume, f.music_muted ? 0.0f : f.music_option * f.music_option);
  sink_.set_parameter(Param::kThrust, f.thrust);
  sink_.set_parameter(Param::kPlayerPosX, f.player_x);
  sink_.set_parameter(Param::kPlayerPosY, f.player_y);
  sink_.set_parameter(Param::kProgressionLevelNumber, float(f.progression_level));
  sink_.set_parameter(Param::kMultiplier, float(f.multiplier));
  sink_.set_parameter(Param::kScore, float(f.score));
  sink_.set_parameter(Param::kNoEnemies, float(f.no_enemies));
  sink_.set_parameter(Param::kTime, float(f.time_left));
  sink_.set_parameter(Param::kLevelTime, float(f.level_time_left));
  sink_.set_parameter(Param::kBombKillCount, float(f.bomb_kill_count));
  sink_.set_parameter(Param::kMagnetActive, f.magnet_active ? 1.0f : 0.0f);
}

void GameAudio::select_music(bool in_game, int mode, bool paused) {
  pause_edge(paused);
  float song_id = 0.0f;
  if (!in_game) {
    sink_.fire(Event::kStartUiMusic);
  } else {
    switch (mode) {
      case 0: case 6: case 7: sink_.fire(Event::kStartSong1); song_id = 1.0f; break;
      case 1: sink_.fire(Event::kStartSong2); song_id = 2.0f; break;
      case 2: sink_.fire(Event::kStartSong3); song_id = 3.0f; break;
      case 3: sink_.fire(Event::kStartSong5); song_id = 5.0f; break;
      case 4: sink_.fire(Event::kStartSong4); song_id = 4.0f; break;
      case 5: sink_.fire(Event::kStartSong6); song_id = 6.0f; break;
      default: return;  // no song and SongID left as it was
    }
  }
  sink_.set_parameter(Param::kSongId, song_id);
}

}  // namespace geow2::audio

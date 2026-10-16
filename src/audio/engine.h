#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>

#include "audio/events.h"
#include "audio/game_audio.h"
#include "audio/graph.h"
#include "audio/waves.h"

namespace geow2::audio {

// The names the game's audio glue uses, read from the original executable
// (nothing of the game is stored in the port): the event table
// dat_8218D588, the parameter table dat_8218D918, the ParameterOut table
// dat_8218D9D0, and the object files and connection names of sub_820799D8
// and sub_82077E58.
struct AudioNames {
  std::array<std::string, kEventCount> events;
  std::array<std::string, kParamCount> params;
  std::string transition_switch;
  // Object files (the name after the last path separator; `%i` = song).
  std::string system_file, fx_file, ui_music_file, song_file;
  // Connections into the System object.
  std::string fx_out, fx_in;
  std::string ui_out, ui_in, mono_out, ui_mono_in;
  std::string song_out, song_in, song_mono_in;  // with `%i`
  std::string renderer_out, wet_out;

  // Reads them from the decrypted image (base 0x82000000); throws
  // FormatError when the image is not the one the port was traced against.
  static AudioNames from_image(std::span<const uint8_t> image);
};

// The renderer's dry voice, as the game's effect on it (sub_820FD630)
// leaves it: the frame at 0.5 (sub_820EC0B8 onto a zeroed buffer), then
// the whole frame zeroed if a sample is above 100 or not finite, or else
// if its RMS (a fused sum of squares in channel order) is above
// MasterEffectRMSMuteThreshold, 5.0 (set by sub_82176DE8). The voice
// reaches the speakers through an identity map at 1.0.
void dry_voice(AudioBuffer& b);

// The audio engine: the nine AOBJ objects of the original (System, FX,
// UIMusic, Song_1..6) run as component graphs and mixed to 5.1 (FL FR C LFE
// SL SR) at 48 kHz in 256-sample frames. Game-thread calls are queued and
// applied at the start of the next audio frame, as the original's message
// queue (sub_820EA738 / sub_820EA898); render() runs the frames.
class AudioEngine : public AudioSink {
 public:
  AudioEngine();
  ~AudioEngine() override;

  // Loads the names from `data/xex/image.bin`, the waves from
  // `data/audio/` and the objects from `data/files/audio/`. False (and
  // silence) on failure, with the reason in `error`.
  bool init(const std::filesystem::path& data_dir, std::string* error = nullptr);

  // Builds the engine from objects in memory: `system` first, then any
  // others whose outputs are connected to System by the names in `names`.
  // Used by init() and by tests.
  void load(const AudioNames& names, WaveStore waves, const Aobj& system, const Aobj* fx, const Aobj* ui_music,
            std::span<const Aobj* const> songs);

  // AudioSink: sub_820787F8 (FX, UIMusic and the songs; not System),
  // sub_820783A8 (FX, UIMusic, System and the songs), the songs' Suspend.
  void fire(Event e) override;
  void set_parameter(Param p, float value) override;
  void suspend_songs(bool suspended) override;

  // sub_82078520: Song_3's ParameterOut TransitionSwitch, 0 without Song_3.
  float transition_switch() const { return transition_.load(std::memory_order_relaxed); }

  // Renders `frames` sample frames of interleaved 6-channel float audio.
  void render(float* out, size_t frames);

  bool loaded() const { return !objects_.empty(); }
  // The audio frame counter.
  uint32_t frame() const { return shared_.frame; }

 private:
  struct Message {
    enum Kind : uint8_t { kSetParameter, kFireAction, kSuspend } kind;
    uint8_t object;
    int32_t component;
    float value;
  };
  struct Slot {
    std::unique_ptr<Object> obj;
    std::array<int32_t, kEventCount> actions;
    std::array<int32_t, kParamCount> params;
  };

  void run_frame();
  void post(const Message& m);

  WaveStore waves_;
  SharedState shared_;
  std::vector<Slot> objects_;  // System, FX, UIMusic, Song_1..6 (missing ones skipped)
  std::vector<uint8_t> songs_;  // indices into objects_
  int32_t song3_object_ = -1, song3_out_ = -1;
  AudioBuffer renderer_;  // what System's OutputToRenderer adds this frame
  std::atomic<float> transition_{0.0f};

  std::mutex queue_lock_;
  std::vector<Message> queue_, draining_;
  std::mutex render_lock_;
  std::vector<float> pending_;  // rendered samples not yet handed out
  size_t pending_at_ = 0;
};

}  // namespace geow2::audio

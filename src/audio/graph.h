#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "audio/aobj.h"

namespace geow2::audio {

class WaveStore;

// The engine renders 48 kHz audio in frames of 256 samples
// (dat_8219787C 48000; the 2.08333e-5 and 256 constants of the Timer and
// LowPass updates; voice nodes render 256-sample blocks).
inline constexpr int kFrameSamples = 256;
inline constexpr int kSampleRate = 48000;
inline constexpr int kMaxChannels = 6;

// The control-update period of every game object (the 5th argument of the
// ObjectManager Load in sub_820778A0): controls tick every second frame.
inline constexpr uint16_t kUpdatePeriod = 2;

// One frame of planar audio. Channel orders: mono; stereo L R; quad FL FR
// SL SR; surround FL FR C SL SR; surround+LFE FL FR C LFE SL SR.
struct AudioBuffer {
  int channels = 0;
  std::array<std::array<float, kFrameSamples>, kMaxChannels> ch{};
  void clear() {
    for (auto& c : ch) c.fill(0.0f);
  }
};

// (src, pin, const): src == -1 is the constant, else component src's
// control output pin (sub_820F1020). When src is set the constant is never
// read.
struct Triple {
  int32_t src = -1;
  uint32_t pin = 0;
  float value = 0.0f;
  bool connected() const { return src != -1; }
};

class Object;
class Manager;

// The component flag bits (vtable slot 16) the runtime uses.
enum ComponentFlags : uint32_t {
  kFlagParameterIn = 0x1,
  kFlagParameterOut = 0x2,
  kFlagActionIn = 0x4,
  kFlagStartable = 0x80,     // a valid target of Start (vt+0x50)
  kFlagSettable = 0x200,     // a Script may store into it (Variable)
  kFlagAudioOut = 0x10000,   // has an audio output a mixer can take
  kFlagUpdate = 0x10000000,  // updated on list +0x50
  kFlagGenerate = 0x20000000,  // list +0x60
  kFlagPrepare = 0x40000000,   // list +0x70
  kFlagScript = 0x80000000,  // updated on list +0x80, before the others
};

// One component of an object (Bizarre Creations' Audio::Component). The
// virtual functions mirror the original's vtable slots.
class Component {
 public:
  virtual ~Component() = default;

  uint32_t type = 0;
  uint32_t index = 0;
  std::string name;

  virtual uint32_t flags() const { return 0; }                         // slot 16
  virtual void init_state(Object&) {}                                  // slot 3
  virtual void set_value(Object&, float) {}                            // slot 4
  virtual void receive_event(Object&, uint32_t /*pin*/) {}             // slot 5
  virtual uint32_t event_inputs() const { return 0; }                  // slot 6
  virtual void update(Object&) {}                                      // slot 8
  virtual float control(Object&, uint32_t /*pin*/) { return 0.0f; }   // slot 13
  virtual void start(Object&) {}                                       // slot 20 (vt+0x50)

  // Audio. A component with audio output is a node of the core graph: its
  // render() fills `out` for the current frame and returns whether it is
  // active (an inactive node's output is not mixed). Nodes are pulled by
  // their consumers and cache their frame themselves.
  virtual int out_channels() const { return 0; }
  virtual bool render(Object&) { return false; }
  // The per-instance generation lists of sub_82108AD0: Prepare (list +0x70,
  // the audio inputs) then Generate (list +0x60, the audio outputs).
  virtual void prepare(Object&) {}
  virtual void generate(Object&) {}
  // An effect attached to a mixer: its unit 0 processes the mixer's output
  // in place (sub_8210DF60).
  virtual void process_effect(Object&, AudioBuffer&) {}

  AudioBuffer out;
};

// The state the components share through the manager (the global audio
// struct dat_821F9EE8, defaults from sub_820E9EA0).
struct SharedState {
  uint32_t frame = 0;  // +0xFAE0, stamps the per-frame caches; 0 at creation
  uint32_t rng = 0;    // +0xFADC, Random and Selector; 0 at creation

  // s = (s + 0x18653) * 0x11BC9 (u32).
  uint32_t next_random() {
    rng = (rng + 0x18653u) * 0x11BC9u;
    return rng;
  }
};

// A loaded object: the one instance of an AOBJ definition.
class Object {
 public:
  Object(SharedState& shared, const Aobj& def, const WaveStore* waves);

  SharedState& shared;
  std::string name;
  // inst+0xC: the update period the dt formulas use; 1 until the first
  // generation, then N (sub_82108080, sub_820F6368).
  uint16_t n = 1;
  // inst+0xE: the frame's position in the control period, (sub + 1) % N at
  // each generation, starting from 0.
  uint16_t sub = 0;
  // The gain-ramp context of the generation (ctx+0xC, ctx+0x10):
  // fres((float)N) * sub and fres((float)N).
  float ramp_start = 0.0f, ramp_span = 0.5f;
  bool suspended = false;  // inst+0x28
  std::vector<std::unique_ptr<Component>> comps;

  uint32_t frame() const { return shared.frame; }
  // The control-update interval in seconds as the components compute it:
  // ((float)n * 2.08333e-05f) * 256.0f.
  float dt() const;

  float read(const Triple& t) { return t.src < 0 ? t.value : control(uint32_t(t.src), t.pin); }
  float control(uint32_t c, uint32_t pin) { return c < comps.size() ? comps[c]->control(*this, pin) : 0.0f; }
  // sub_820F1080: Start a component (flag 0x80 required).
  void start(int32_t c);
  // Send event `pin` to component c (sub_821215D0).
  void event(int32_t c, uint32_t pin);
  // Pulls node c for the current frame: true when it is active (its
  // output is then in comps[c]->out).
  bool pull(int32_t c);

  // One control update (sub_82108930): Scripts first, then the other
  // updated components, each in component order.
  void control_update();

  // The generation of one frame (sub_820F6368 for this instance, then
  // sub_82108AD0).
  void generate_frame();

  // Name lookups (Object slots 8/10/11): the first component of that name
  // with the flag, else -1.
  int32_t find(const std::string& name, uint32_t flag) const;

 private:
  std::vector<Component*> scripts_, updated_, prepare_, generate_;
};

// Reads the triple at body[k], body[k+1], body[k+2], and the pair (a
// connected input without a constant) at body[k], body[k+1].
Triple triple_at(const AobjRecord& r, size_t k);
Triple pair_at(const AobjRecord& r, size_t k);

}  // namespace geow2::audio

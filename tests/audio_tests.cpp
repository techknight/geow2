// Unit tests for the audio engine, on small objects and waves built here
// (no game data).

#include <bit>
#include <cmath>
#include <limits>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "audio/aobj.h"
#include "audio/components.h"
#include "audio/engine.h"
#include "audio/game_audio.h"
#include "audio/graph.h"
#include "audio/reverb.h"
#include "audio/waves.h"
#include "extract/bytes.h"

using namespace geow2;
using namespace geow2::audio;

static int failures = 0;

static void expect(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

static bool near(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps; }

static uint32_t f(float v) { return std::bit_cast<uint32_t>(v); }
static constexpr uint32_t kNone = 0xFFFFFFFFu;

// Builds an AOBJ file: records are (type, body words); names are given as
// strings and stored in the string table.
class Builder {
 public:
  uint32_t str(const std::string& s) {
    const uint32_t at = uint32_t(strings_.size());
    strings_.insert(strings_.end(), s.begin(), s.end());
    strings_.push_back(0);
    return at;
  }
  int add(uint32_t type, const std::string& name, std::vector<uint32_t> rest) {
    std::vector<uint32_t> body{str(name)};
    body.insert(body.end(), rest.begin(), rest.end());
    records_.emplace_back(type, std::move(body));
    return int(records_.size() - 1);
  }
  Bytes build(const std::string& object_name = "Obj") {
    const uint32_t name_at = str(object_name);
    std::vector<uint32_t> words;
    for (auto& [t, b] : records_) {
      words.push_back(t);
      words.insert(words.end(), b.begin(), b.end());
    }
    Bytes out;
    auto put = [&](uint32_t v) {
      for (int i = 0; i < 4; ++i) out.push_back(uint8_t(v >> (24 - 8 * i)));
    };
    put(0x414F424A);
    put(0x00356DA0);
    put(uint32_t(0x14 + 4 * words.size()));
    put(name_at);
    put(uint32_t(records_.size()));
    for (uint32_t w : words) put(w);
    out.insert(out.end(), strings_.begin(), strings_.end());
    return out;
  }
  Aobj aobj(const std::string& object_name = "Obj") { return parse_aobj(build(object_name)); }

 private:
  std::vector<uint8_t> strings_;
  std::vector<std::pair<uint32_t, std::vector<uint32_t>>> records_;
};

// A triple holding a constant.
static std::vector<uint32_t> k(float v) { return {kNone, 0, f(v)}; }
// A triple reading component c's pin 0.
static std::vector<uint32_t> in(int c) { return {uint32_t(c), 0, 0}; }

static std::vector<uint32_t> cat(std::initializer_list<std::vector<uint32_t>> parts) {
  std::vector<uint32_t> out;
  for (const auto& p : parts) out.insert(out.end(), p.begin(), p.end());
  return out;
}

static void run_updates(Object& o, int n) {
  for (int i = 0; i < n; ++i) {
    o.control_update();
    ++o.shared.frame;
  }
}

static void test_reader() {
  Builder b;
  b.add(0x12, "Var", {f(2.5f)});
  b.add(0x1F, "Sum", cat({{2}, k(1.0f), in(0)}));
  b.add(0xA9, "Script", {kNone, 2, 0x1202, 0});
  b.add(0x66, "", {});  // a zero-word body
  const Bytes file = b.build("Thing");
  const Aobj a = parse_aobj(file);
  expect(a.name == "Thing", "aobj: object name");
  expect(a.records.size() == 4, "aobj: record count");
  expect(a.records[1].type == 0x1F && a.records[1].body.size() == 8, "aobj: Sum body is 2 + 3n words");
  expect(a.records[2].body.size() == 5, "aobj: Script body is 3 + length words");
  expect(a.string_at(a.records[0].body[0]) == "Var", "aobj: component name");
  expect(a.records[1].offset == 0x14 + 4 * 3, "aobj: record offset");

  Bytes bad = file;
  bad[0] = 'X';
  bool threw = false;
  try {
    parse_aobj(bad);
  } catch (const FormatError&) {
    threw = true;
  }
  expect(threw, "aobj: bad magic rejected");

  Builder u;
  u.add(0x05, "Unknown", {});
  threw = false;
  try {
    u.aobj();
  } catch (const FormatError&) {
    threw = true;
  }
  expect(threw, "aobj: unknown component type rejected");

  Bytes cut = file;
  cut.resize(0x18);
  threw = false;
  try {
    parse_aobj(cut);
  } catch (const FormatError&) {
    threw = true;
  }
  expect(threw, "aobj: truncated file rejected");

  // A damaged record count (found by the fuzzer: it sized a 90 GB
  // reservation) is refused before anything is allocated for it.
  Bytes huge = file;
  huge[0x10] = 0x7F, huge[0x11] = 0xFF, huge[0x12] = 0xFF, huge[0x13] = 0xFF;
  threw = false;
  try {
    parse_aobj(huge);
  } catch (const FormatError&) {
    threw = true;
  }
  expect(threw, "aobj: a record count beyond the file rejected");
}

static void test_script() {
  // Var = 2 + 3; if Var > 4 then Out = 1 else Out = -1; Count += 1 via EVENT
  // on a Timer is not needed here: start the second script from the first.
  Builder b;
  const int var = b.add(0x12, "Var", {f(0.0f)});
  const int out = b.add(0x12, "Out", {f(0.0f)});
  // Script 2 (index 3, higher than script 1) runs in the same update.
  const std::vector<uint32_t> code1 = {
      0x1004, f(2.0f), 0x1305, f(3.0f), 0x1002, uint32_t(var),   // Var = 2 + 3
      0x1001, uint32_t(var), 0x1004, f(4.0f), 0x1300,            // compare Var with 4
      0x1106, 18,                                                // not greater: jump
      0x1003, 0x1003, 0x1004, f(1.0f), 0x1002, uint32_t(out),    // Out = 1 ... (index 13..19)
      0x1200, 3, 0x1202};
  // Fix the jump target: the else branch is past the END.
  std::vector<uint32_t> c1 = code1;
  c1[12] = uint32_t(c1.size());
  c1.insert(c1.end(), {0x1004, f(-1.0f), 0x1002, uint32_t(out), 0x1202});
  b.add(0xA9, "S1", cat({{kNone, uint32_t(c1.size())}, c1}));
  const std::vector<uint32_t> c2 = {0x1001, uint32_t(out), 0x1307, f(10.0f), 0x1002, uint32_t(out), 0x1202};
  b.add(0xA9, "S2", cat({{kNone, uint32_t(c2.size())}, c2}));
  const Aobj a = b.aobj();
  SharedState shared;
  Object o(shared, a, nullptr);
  o.start(2);
  run_updates(o, 1);
  expect(near(o.control(uint32_t(var), 0), 5.0f), "script: push, add constant, pop into Variable");
  expect(near(o.control(uint32_t(out), 0), 10.0f), "script: compare, branch, and a higher script started runs in the same update");
  // Nothing runs without a Start.
  run_updates(o, 2);
  expect(near(o.control(uint32_t(out), 0), 10.0f), "script: runs only when started");
}

static void test_plf_unsorted() {
  Builder b;
  const int x = b.add(0x12, "X", {f(0.0f)});
  // Points (0, 0), (10, 10), (5, 50), (20, 20): the scan stops at the first
  // x_i >= x without sorting.
  const int plf = b.add(0x13, "Curve", cat({{uint32_t(x), 0}, k(1.0f), {4}, {f(0.0f), f(0.0f), kNone, 0},
                                            {f(10.0f), f(10.0f), kNone, 0}, {f(5.0f), f(50.0f), kNone, 0},
                                            {f(20.0f), f(20.0f), kNone, 0}}));
  const Aobj a = b.aobj();
  SharedState shared;
  Object o(shared, a, nullptr);
  auto at = [&](float v) {
    o.comps[size_t(x)]->set_value(o, v);
    ++shared.frame;
    return o.control(uint32_t(plf), 0);
  };
  expect(near(at(5.0f), 5.0f), "plf: interpolates between the first two points");
  // x = 12: the scan passes 10 and 5 (5 < 12) and stops at 20, blending
  // (5, 50) and (20, 20): 50 + (12 - 5) / 15 * (20 - 50) = 36.
  expect(near(at(12.0f), 36.0f, 1e-4f), "plf: unsorted points keep the linear scan");
  expect(near(at(-1.0f), 0.0f), "plf: below the first point");
  expect(near(at(25.0f), 20.0f), "plf: past the last point");
  // Cached within a frame.
  o.comps[size_t(x)]->set_value(o, 5.0f);
  expect(near(o.control(uint32_t(plf), 0), 20.0f), "plf: cached for the frame");
}

static void test_gradient_quirk() {
  Builder b;
  const int x = b.add(0x12, "X", {f(0.0f)});
  const int g = b.add(0x26, "Grad", {uint32_t(x), 0});
  const Aobj a = b.aobj();
  SharedState shared;
  Object o(shared, a, nullptr);
  o.n = kUpdatePeriod;
  o.comps[size_t(x)]->set_value(o, 1.0f);
  run_updates(o, 1);
  // (x - prev) * N * 48000 / 256 = 1 * 2 * 187.5: multiplied by the period.
  expect(near(o.control(uint32_t(g), 0), 375.0f), "gradient: multiplies by the update period");
  run_updates(o, 1);
  expect(near(o.control(uint32_t(g), 0), 0.0f), "gradient: zero for a constant input");
}

static void test_timer_gate() {
  Builder b;
  const int fired = b.add(0x12, "Fired", {f(0.0f)});
  const std::vector<uint32_t> code = {0x1001, uint32_t(fired), 0x1305, f(1.0f), 0x1002, uint32_t(fired), 0x1202};
  const int script = b.add(0xA9, "OnAlarm", cat({{kNone, uint32_t(code.size())}, code}));
  const int timer = b.add(0x1A, "Timer", cat({k(0.0f), k(1.0f), k(0.05f), {uint32_t(script)}}));
  const int gate = b.add(0x30, "Gate", cat({{uint32_t(timer), 0}, k(0.5f), k(0.0f), k(-1.0f), k(1.0f),
                                             {uint32_t(script), kNone}}));
  const Aobj a = b.aobj();
  SharedState shared;
  Object o(shared, a, nullptr);
  o.n = kUpdatePeriod;
  o.event(timer, 0);  // run
  for (int i = 0; i < 5; ++i) run_updates(o, 1);
  // dt = 2 * 256 / 48000; after 5 updates the time is 0.0533 >= 0.05: the
  // alarm started the script, which runs at the next update.
  expect(near(o.control(uint32_t(timer), 0), 5 * (2.0f * 2.08333331e-05f * 256.0f), 1e-5f), "timer: time advances by dt * rate");
  run_updates(o, 1);
  expect(near(o.control(uint32_t(fired), 0), 1.0f), "timer: the alarm starts its target once");
  // GateSwitch output then input within one frame (the stamp quirk).
  for (int i = 0; i < 100; ++i) run_updates(o, 1);
  const float first = o.control(uint32_t(gate), 0);
  const float second = o.control(uint32_t(gate), 0);
  expect(near(first, 1.0f), "gateswitch: high output above the threshold");
  expect(second > 0.5f && !near(second, 1.0f), "gateswitch: a second read in the frame returns the input");
  expect(near(o.control(uint32_t(fired), 0), 2.0f), "gateswitch: rising edge starts its target");
}

static void test_selector_random() {
  Builder b;
  const int sel = b.add(0xAA, "Sel", cat({{3}, k(0.0f), k(10.0f), k(1.0f), k(0.0f), k(1.0f), k(20.0f), k(1.0f), k(0.0f),
                                          k(1.0f), k(30.0f), k(1.0f), k(0.0f), k(1.0f)}));
  const int rnd = b.add(0x1D, "Rnd", cat({k(0.0f), k(3.0f)}));
  const Aobj a = b.aobj();
  SharedState shared;
  Object o(shared, a, nullptr);
  // With multiplier 0 and increment 1, every entry is picked once in three.
  bool seen[3] = {};
  for (int i = 0; i < 3; ++i) {
    o.event(sel, 0);
    const float v = o.control(uint32_t(sel), 0);
    const int idx = int(v / 10.0f) - 1;
    if (idx >= 0 && idx < 3) seen[idx] = true;
  }
  expect(seen[0] && seen[1] && seen[2], "selector: least recently picked wins with multiplier 0");
  // Random event 2 never repeats the current value.
  float last = o.control(uint32_t(rnd), 0);
  bool repeated = false;
  for (int i = 0; i < 50; ++i) {
    o.event(rnd, 2);
    const float v = o.control(uint32_t(rnd), 0);
    if (v == last || v < 0.0f || v > 3.0f || v != std::floor(v)) repeated = true;
    last = v;
  }
  expect(!repeated, "random: event 2 gives a different integer in range");
  // The generator: s = (s + 0x18653) * 0x11BC9.
  SharedState s2;
  expect(s2.next_random() == 0x18653u * 0x11BC9u, "random: generator step from seed 0");
}

static void test_control_lowpass() {
  Builder b;
  const int x = b.add(0x12, "X", {f(1.0f)});
  const int lp = b.add(0x18, "Smooth", cat({{uint32_t(x), 0}, k(1.0f)}));
  const Aobj a = b.aobj();
  SharedState shared;
  Object o(shared, a, nullptr);
  o.n = kUpdatePeriod;
  float y = 0.0f;
  for (int i = 0; i < 2000; ++i) {
    run_updates(o, 1);
    y = o.control(uint32_t(lp), 0);
  }
  expect(near(y, 1.0f, 1e-3f), "lowpass: settles at the input");
  // First step: a1 = -((K-1)/(K+1)), a0 = (1-a1)/2, y = (0 + 1) * a0.
  Object o2(shared, a, nullptr);
  o2.n = kUpdatePeriod;
  o2.control_update();
  const float kk = float(std::atan(double((2.0f * 2.08333331e-05f * 256.0f) * 3.14159274f * 1.0f)));
  const float a1 = -((kk - 1.0f) / (kk + 1.0f));
  expect(near(o2.control(uint32_t(lp), 0), (1.0f - a1) * 0.5f, 1e-6f), "lowpass: atan-based coefficient");
}

// A mono input mixed at 0.5 into an output: the mixer is silent in the
// frame its input first becomes active, then mixes.
static void test_mixer() {
  Builder b;
  const int src = b.add(0x0C, "In", {});
  const int mix = b.add(0x3D, "Mix", cat({k(0.5f), {0, 1}, {uint32_t(src)}, k(1.0f)}));
  b.add(0x0D, "Out", {uint32_t(mix)});
  const Aobj a = b.aobj();
  SharedState shared;
  Object o(shared, a, nullptr);
  AudioBuffer dest;
  dest.channels = 1;
  connect_audio_output(*o.comps[2], &dest);
  AudioBuffer* acc = audio_input_accumulator(*o.comps[size_t(src)]);
  expect(acc != nullptr, "mixer: input accumulator");
  float got[3] = {};
  for (int fr = 0; fr < 3; ++fr) {
    for (float& s : acc->ch[0]) s = 1.0f;
    dest.clear();
    if (shared.frame % kUpdatePeriod == 0) o.control_update();
    o.generate_frame();
    got[fr] = dest.ch[0][100];
    ++shared.frame;
  }
  expect(near(got[0], 0.0f), "mixer: silent in the first active frame");
  expect(near(got[1], 0.5f), "mixer: input gain * master");
  expect(near(got[2], 0.5f), "mixer: steady");
  expect(near(core_sqrt(0.25f), 0.5f) && core_sqrt(-1.0f) == 0.0f && core_sqrt(0.0f) == 0.0f, "mixer: square root");
}

// A PCM one-shot through the bypass path: one sample late, then Finished
// starts a script.
static void test_source() {
  WaveStore store;
  WaveBank bank;
  bank.name = "TestBank";
  Wave w;
  w.name = "Ramp";
  w.format = 3;
  w.rate = 48000;
  w.channels = 1;
  for (int i = 0; i < 300; ++i) w.samples.push_back(int16_t(i * 100));
  bank.waves.push_back(w);
  store.add(bank);
  expect(store.find("testbank", "RAMP") != nullptr, "waves: names match without case");
  expect(store.find("TestBank", "Other") == nullptr, "waves: missing wave");

  Builder b;
  const int done = b.add(0x12, "Done", {f(0.0f)});
  const std::vector<uint32_t> code = {0x1004, f(1.0f), 0x1002, uint32_t(done), 0x1202};
  const int script = b.add(0xA9, "Finished", cat({{kNone, uint32_t(code.size())}, code}));
  const int snd = b.add(0x34, "Sound", cat({{b.str("testBANK"), b.str("ramp"), 1}, k(0.0f), k(1.0f), k(0.0f), k(1.0f),
                                           {uint32_t(script), 0}}));
  b.add(0x0D, "Out", {uint32_t(snd)});
  const Aobj a = b.aobj();
  SharedState shared;
  Object o(shared, a, &store);
  AudioBuffer dest;
  dest.channels = 1;
  connect_audio_output(*o.comps[3], &dest);
  o.control_update();
  o.event(snd, 0);  // Play
  expect(near(o.control(uint32_t(snd), 1), 1.0f), "source: one active voice");
  dest.clear();
  o.generate_frame();
  ++shared.frame;
  expect(dest.ch[0][0] == 0.0f && near(dest.ch[0][1], 0.0f) && near(dest.ch[0][2], 100.0f / 32768.0f),
         "source: PCM plays one sample late");
  dest.clear();
  o.generate_frame();
  ++shared.frame;
  expect(near(dest.ch[0][0], 255.0f * 100.0f / 32768.0f), "source: the held sample starts the next block");
  expect(near(dest.ch[0][44], 299.0f * 100.0f / 32768.0f) && dest.ch[0][45] == 0.0f, "source: zeros past the end");
  o.control_update();
  expect(near(o.control(uint32_t(done), 0), 1.0f), "source: Finished starts its target");
  expect(near(o.control(uint32_t(snd), 1), 0.0f), "source: no active voice after Finished");
}

class Recorder : public AudioSink {
 public:
  std::vector<int> events;
  std::vector<std::pair<int, float>> params;
  std::vector<bool> suspends;
  void fire(Event e) override { events.push_back(int(e)); }
  void set_parameter(Param p, float v) override { params.emplace_back(int(p), v); }
  void suspend_songs(bool s) override { suspends.push_back(s); }
  void reverb_readback() override { params.emplace_back(-1, 0.0f); }
};

static void test_glue() {
  Recorder r;
  GameAudio g(r);
  g.pickup_geom(100);
  g.pickup_geom(120);  // inside the 35 ms gate
  g.pickup_geom(135);
  g.pickup_geom(171);
  expect(r.events.size() == 3, "glue: 35 ms gate");
  expect(r.events[0] == 0x48 && r.events[1] == 0x47 && r.events[2] == 0x48, "glue: alternating pair, second id first");
  r.events.clear();
  g.power_up_end(Event::kPowerUpEndSpeed, 1000);
  g.power_up_end(Event::kPowerUpEndSlow, 1010);
  expect(r.events.size() == 1 && r.events[0] == 0x67, "glue: one gate for all PowerUpEnd events");

  r.events.clear();
  r.params.clear();
  g.select_music(true, 3, false);
  expect(r.events.size() == 1 && r.events[0] == 0x21, "glue: Waves plays song 5");
  expect(r.params.size() == 1 && r.params[0].first == 16 && r.params[0].second == 5.0f, "glue: SongID 5");
  r.events.clear();
  r.params.clear();
  g.select_music(true, 7, false);
  expect(r.events[0] == 0x1D && r.params[0].second == 1.0f, "glue: modes 0, 6 and 7 play song 1");
  r.events.clear();
  r.params.clear();
  g.select_music(false, 0, true);
  expect(r.events[0] == 0x1C && r.params[0].second == 0.0f, "glue: menu music, SongID 0");
  expect(r.suspends.size() == 1 && r.suspends[0], "glue: the pause edge suspends the songs");

  r.params.clear();
  g.gravity_well_hits(1, 3);
  g.gravity_well_hits(2, 6);
  g.gravity_wells_update(6);
  expect(r.params.size() == 5 && near(r.params[0].second, 0.5f) && near(r.params[1].second, 1.0f) &&
             r.params[2].second == 0.0f,
         "glue: well hum slots hits / max");
  r.params.clear();
  g.gravity_well_hits(2, 3);
  g.gravity_wells_update(6);
  expect(r.params[0].second == 0.0f && near(r.params[1].second, 0.5f), "glue: unreported wells free their slot");

  AudioFrame fr;
  fr.paused = false;
  fr.sfx_option = 0.5f;
  r.params.clear();
  g.frame(fr);
  bool sfx = false;
  for (auto& [p, v] : r.params)
    if (p == 8 && near(v, 0.25f)) sfx = true;
  expect(sfx, "glue: the engine receives the volume squared");
  // sub_82079BC0 reads the reverb parameters back between MayflyNumbers and DistanceFromHill.
  size_t mayfly = 0, readback = 0, hill = 0;
  for (size_t i = 0; i < r.params.size(); ++i) {
    if (r.params[i].first == 2) mayfly = i;
    if (r.params[i].first == -1) readback = i;
    if (r.params[i].first == 14) hill = i;
  }
  expect(readback == mayfly + 1 && hill == readback + 1, "glue: reverb readback between MayflyNumbers and DistanceFromHill");
  expect(remaining_seconds(60, 70) == 0 && remaining_seconds(60, 10) == 50, "glue: remaining seconds");
}

// The engine: names, a System object and an FX object joined by name; FX's
// audio reaches the renderer one frame late; events and parameters are
// queued to the next frame.
static void test_engine() {
  AudioNames names;
  for (int i = 0; i < kEventCount; ++i) names.events[size_t(i)] = "Event" + std::to_string(i);
  for (int i = 0; i < kParamCount; ++i) names.params[size_t(i)] = "Param" + std::to_string(i);
  names.transition_switch = "Switch";
  names.fx_out = "FxOut";
  names.fx_in = "FxIn";
  names.renderer_out = "ToRenderer";
  names.mono_out = "Mono";

  Builder sys;
  const int fx_in = sys.add(0xB8, "FxIn", {});
  sys.add(0xB9, "ToRenderer", {uint32_t(fx_in)});
  const Aobj system = sys.aobj("System");

  Builder fx;
  const int level = fx.add(0x00, "Param8", {f(0.0f)});  // SFX_Volume
  const int seen = fx.add(0x12, "Seen", {f(0.0f)});
  const std::vector<uint32_t> code = {0x1004, f(1.0f), 0x1002, uint32_t(seen), 0x1202};
  const int script = fx.add(0xA9, "OnEvent", cat({{kNone, uint32_t(code.size())}, code}));
  fx.add(0x02, "Event5", {uint32_t(script)});
  const int src = fx.add(0x0C, "Feed", {});
  const int lfe = fx.add(0xB5, "Pan", cat({k(1.0f), k(0.0f), k(0.0f), k(3.4e38f), k(0.0f), {0, 1}, {uint32_t(src)}, in(level)}));
  fx.add(0xB9, "FxOut", {uint32_t(lfe)});
  const Aobj fxo = fx.aobj("FX");

  AudioEngine e;
  e.load(names, WaveStore{}, system, &fxo, nullptr, {});
  expect(e.loaded(), "engine: loaded");
  e.set_parameter(Param::kSfxVolume, 1.0f);
  e.fire(Event::kFireNormal);
  std::vector<float> out(256 * 6 * 4);
  e.render(out.data(), 256 * 4);
  expect(e.frame() == 4, "engine: four frames rendered");
  // Nothing feeds the FX input, so the output is silent; the event ran.
  bool silent = true;
  for (float v : out) silent = silent && v == 0.0f;
  expect(silent, "engine: silence without sources");
  expect(e.transition_switch() == 0.0f, "engine: no Song_3, TransitionSwitch 0");
}

// The dry voice: half level; a sample above 100, a NaN or an RMS above 5
// silences the whole frame.
static void test_dry_voice() {
  AudioBuffer b;
  b.channels = 6;
  b.ch[3][7] = 3.0f;
  dry_voice(b);
  expect(b.ch[3][7] == 1.5f, "dry voice: half level");
  b.ch[0][0] = 201.0f;
  dry_voice(b);
  expect(b.ch[3][7] == 0.0f && b.ch[0][0] == 0.0f, "dry voice: above 100 silences the frame");
  b.ch[5][255] = std::numeric_limits<float>::quiet_NaN();
  b.ch[1][1] = 1.0f;
  dry_voice(b);
  expect(b.ch[1][1] == 0.0f && b.ch[5][255] == 0.0f, "dry voice: NaN silences the frame");
  // RMS after halving: 11 on every sample is 5.5 > 5; 10 is 5, not above.
  for (auto& c : b.ch) c.fill(11.0f);
  dry_voice(b);
  expect(b.ch[2][100] == 0.0f, "dry voice: RMS above 5 silences the frame");
  for (auto& c : b.ch) c.fill(10.0f);
  dry_voice(b);
  expect(b.ch[2][100] == 5.0f, "dry voice: RMS of 5 passes");
}

// The reverb's pieces whose results follow from the trace alone.
static void test_reverb_setup() {
  expect(reverb_length(0) == 1 && reverb_length(1) == 1 && reverb_length(9973) == 9973 && reverb_length(20000) == 9973,
         "reverb: length lookup bounds");
  expect(reverb_length(509) == 509 && reverb_length(10) == 7 && reverb_length(40) == 37 && reverb_length(3432) == 3413,
         "reverb: the lookup's binary search is not a nearest-prime search");

  I3dl2 p;
  p.room = -1000;
  p.reflections_delay = 0.02f;
  p.reverb_delay = 0.04f;
  p.diffusion = 100.0f;
  p.decay_time = 2.0f;
  p.decay_hf_ratio = 0.5f;
  ReverbNative n = i3dl2_to_native(p);
  expect(n.reflections_delay == 20 && n.reverb_delay == 40, "reverb: delays in ms");
  expect(n.diffusion_early == 15 && n.diffusion_late == 15, "reverb: diffusion 100 -> index 15");
  expect(n.low_gain == 8 && n.high_gain == 7 && n.decay == 2.0f, "reverb: HF ratio 0.5 -> high gain 7, decay kept");
  expect(n.room_main == -10.0f && n.position_l == 6 && n.matrix_r == 27 && n.rear_delay == 5 && n.room_size == 100.0f,
         "reverb: fixed native fields");
  p.decay_hf_ratio = 2.0f;
  p.reflections_delay = 0.5f;
  p.reverb_delay = 0.2f;
  n = i3dl2_to_native(p);
  expect(n.high_gain == 8 && n.low_gain == 7 && n.decay == 4.0f, "reverb: HF ratio 2 -> low gain 7, decay scaled");
  expect(n.reflections_delay == 299 && n.reverb_delay == 84, "reverb: delay clamps");
  p.reflections_delay = 0.0f;
  expect(i3dl2_to_native(p).reflections_delay == 1, "reverb: reflections delay at least 1 ms");

  AudioBuffer speakers;
  speakers.channels = 6;
  speakers.ch[0].fill(1.0f);
  std::array<float, kFrameSamples> wet{};
  wet.fill(0.25f);
  add_wet_to_speakers(wet, speakers);
  expect(speakers.ch[0][9] == 1.5f && speakers.ch[1][9] == 0.5f && speakers.ch[2][9] == 0.0f &&
             speakers.ch[4][9] == 0.0f,
         "reverb: channel 0 added twice to FL and FR only");

  AudioBuffer mono;
  mono.channels = 1;
  mono.ch[0].fill(11.0f);
  wet_voice(mono);
  expect(mono.ch[0][0] == 0.0f, "reverb: the wet voice's RMS mute");
  mono.ch[0].fill(4.0f);
  wet_voice(mono);
  expect(mono.ch[0][0] == 2.0f, "reverb: the wet voice at half level");
}

// Runs `frames` frames of a mono input through the reverb; returns FL and FR.
static std::pair<std::vector<float>, std::vector<float>> run_reverb(Reverb& r, const std::vector<float>& in) {
  std::vector<float> fl, fr;
  Reverb::Output o;
  for (size_t at = 0; at < in.size(); at += kFrameSamples) {
    r.process(in.data() + at, in.data() + at, o);
    fl.insert(fl.end(), o.fl.begin(), o.fl.end());
    fr.insert(fr.end(), o.fr.begin(), o.fr.end());
  }
  return {fl, fr};
}

static void test_reverb_process() {
  // Silence stays silent, from the initial state.
  {
    Reverb r;
    const auto [fl, fr] = run_reverb(r, std::vector<float>(kFrameSamples * 8, 0.0f));
    bool silent = true;
    for (float v : fl) silent = silent && v == 0.0f;
    for (float v : fr) silent = silent && v == 0.0f;
    expect(silent, "reverb: silence in, silence out");
  }
  // Only T1's tap gain set (T_370[6] = 0.5, Room 0 dB, Reflections 0 dB):
  // every other path is zero and every allpass is a pure delay, so an
  // impulse reaches FL after the pre-delay plus 251 samples (array carries,
  // the room filter, T1's one-sample tap, the output allpasses 131 and 113)
  // at 2 x 0.5, and FR after the pre-delay plus 241 (allpasses 107, 127).
  {
    ReverbTables t;
    t.words[(0x8201F370 + 6 * 4 - ReverbTables::kBase) / 4] = 0.5f;
    Reverb r(&t);
    I3dl2 p;
    p.room = 0;
    p.room_hf = 0;
    p.reflections = 0;
    p.reflections_delay = 0.02f;  // 960 samples
    r.set_i3dl2(p);
    const size_t t0 = size_t(kFrameSamples) * 40 + 77;  // after the 5000-sample fade
    std::vector<float> in(size_t(kFrameSamples) * 48, 0.0f);
    in[t0] = 1.0f;
    const auto [fl, fr] = run_reverb(r, in);
    const size_t at_l = t0 + 960 + 251, at_r = t0 + 960 + 241;
    bool rest = true;
    for (size_t i = 0; i < fl.size(); ++i) {
      if (i != at_l && fl[i] != 0.0f) rest = false;
      if (i != at_r && fr[i] != 0.0f) rest = false;
    }
    expect(near(fl[at_l], 1.0f, 1e-3f) && near(fr[at_r], 1.0f, 1e-3f), "reverb: early tap delay and gain");
    expect(rest, "reverb: nothing else on the early tap path");
  }
  // Every operation is linear in the signal: twice the input gives exactly
  // twice the output; the mono input comes out decorrelated.
  {
    ReverbTables t;
    for (float& w : t.words) w = 0.25f;
    I3dl2 p;
    p.room = -100;
    p.room_hf = -300;
    p.reflections = -500;
    p.reverb = -200;
    Reverb a(&t), b(&t);
    a.set_i3dl2(p);
    b.set_i3dl2(p);
    std::vector<float> in(size_t(kFrameSamples) * 40), in2(in.size());
    uint32_t s = 1;
    for (size_t i = 0; i < in.size(); ++i) {
      s = s * 1664525u + 1013904223u;
      in[i] = float(int32_t(s >> 8) - (1 << 23)) / float(1 << 23);
      in2[i] = in[i] * 2.0f;
    }
    const auto [fl1, fr1] = run_reverb(a, in);
    const auto [fl2, fr2] = run_reverb(b, in2);
    bool exact = true, differs = false;
    for (size_t i = 0; i < fl1.size(); ++i) {
      exact = exact && fl2[i] == fl1[i] * 2.0f && fr2[i] == fr1[i] * 2.0f;
      differs = differs || fl1[i] != fr1[i];
    }
    expect(exact, "reverb: linear (2x in, exactly 2x out)");
    expect(differs, "reverb: left and right differ for a mono input");
  }
}

static void run_all() {
  test_reader();
  test_script();
  test_plf_unsorted();
  test_gradient_quirk();
  test_timer_gate();
  test_selector_random();
  test_control_lowpass();
  test_mixer();
  test_source();
  test_glue();
  test_engine();
  test_dry_voice();
  test_reverb_setup();
  test_reverb_process();
}

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  try {
    run_all();
  } catch (const std::exception& e) {
    std::printf("FAIL: exception: %s\n", e.what());
    ++failures;
  }
  if (failures) {
    std::printf("%d failure(s)\n", failures);
    return 1;
  }
  std::printf("audio tests passed\n");
  return 0;
}

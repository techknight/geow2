#include "audio/engine.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "audio/components.h"
#include "extract/bytes.h"

namespace geow2::audio {

namespace {

constexpr uint32_t kImageBase = 0x82000000;

uint32_t image_word(std::span<const uint8_t> img, uint32_t addr) {
  need(img, addr - kImageBase, 4, "audio names");
  return be32(img.data() + (addr - kImageBase));
}

std::string image_string(std::span<const uint8_t> img, uint32_t addr) {
  need(img, addr - kImageBase, 1, "audio names");
  const size_t at = addr - kImageBase;
  const void* end = std::memchr(img.data() + at, 0, img.size() - at);
  if (!end) throw FormatError("audio names: unterminated string");
  return std::string(reinterpret_cast<const char*>(img.data() + at), static_cast<const uint8_t*>(end) - (img.data() + at));
}

// A table of (id, name pointer) pairs ending with a null name.
template <size_t N>
void read_table(std::span<const uint8_t> img, uint32_t addr, std::array<std::string, N>& out) {
  for (;; addr += 8) {
    const uint32_t name = image_word(img, addr + 4);
    if (name == 0) break;
    const uint32_t id = image_word(img, addr);
    if (id >= N) throw FormatError("audio names: id out of range");
    out[id] = image_string(img, name);
  }
}

std::string file_part(const std::string& path) {
  const size_t slash = path.find_last_of("\\/");
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string with_number(const std::string& fmt, int i) {
  std::string s = fmt;
  const size_t at = s.find("%i");
  if (at != std::string::npos) s.replace(at, 2, std::to_string(i));
  return s;
}

bool iequal(const std::string& a, const std::string& b) {
  return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
           return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
         });
}

// A file of `dir` matching `name` without regard to case (the original's
// paths are not cased as the extracted files are).
std::filesystem::path find_file(const std::filesystem::path& dir, const std::string& name) {
  std::error_code ec;
  for (const auto& e : std::filesystem::directory_iterator(dir, ec))
    if (iequal(e.path().filename().string(), name)) return e.path();
  return {};
}

}  // namespace

void dry_voice(AudioBuffer& b) {
  const int n = b.channels;
  bool zero = false;
  for (int c = 0; c < n; ++c)
    for (float& x : b.ch[size_t(c)]) {
      x = 0.5f * x + 0.0f;  // dat_82000724, vmaddfp onto zeros (a -0 becomes +0)
      if (x > 100.0f || !std::isfinite(x)) zero = true;  // dat_82001020, sub_82133D40
    }
  if (!zero) {
    float acc = 0.0f;
    for (int c = 0; c < n; ++c)
      for (float x : b.ch[size_t(c)]) acc = std::fmaf(x, x, acc);
    zero = std::sqrt(acc / float(n * kFrameSamples)) > 5.0f;  // dat_820006F0
  }
  if (zero) b.clear();
}

void add_wet_to_speakers(const std::array<float, kFrameSamples>& wet, AudioBuffer& speakers) {
  static constexpr int kMap[4] = {0, 1, 0, 1};
  for (int out : kMap)
    for (int i = 0; i < kFrameSamples; ++i)
      speakers.ch[size_t(out)][size_t(i)] = std::fma(wet[size_t(i)], 1.0f, speakers.ch[size_t(out)][size_t(i)]);
}

AudioNames AudioNames::from_image(std::span<const uint8_t> img) {
  AudioNames n;
  read_table(img, 0x8218D588, n.events);
  read_table(img, 0x8218D918, n.params);
  std::array<std::string, 1> outs;
  read_table(img, 0x8218D9D0, outs);
  n.transition_switch = outs[0];
  // sub_820799D8 and sub_82077E58.
  n.system_file = file_part(image_string(img, 0x82003B84));
  n.fx_file = file_part(image_string(img, 0x820039CC));
  n.ui_music_file = file_part(image_string(img, 0x82003990));
  n.song_file = file_part(image_string(img, 0x82003900));
  n.fx_out = image_string(img, 0x820039B8);
  n.fx_in = image_string(img, 0x820039C4);
  n.ui_out = image_string(img, 0x82003970);
  n.ui_in = image_string(img, 0x82003980);
  n.mono_out = image_string(img, 0x82003950);
  n.ui_mono_in = image_string(img, 0x8200395C);
  n.song_out = image_string(img, 0x82003918);
  n.song_in = image_string(img, 0x82003928);
  n.song_mono_in = image_string(img, 0x82003934);
  n.renderer_out = image_string(img, 0x82003B70);
  n.wet_out = image_string(img, 0x82003B5C);
  const uint32_t reverb_names[12] = {0x82003B44, 0x82003B28, 0x82003B04, 0x82003AE8, 0x82003AC8, 0x82003AA8,
                                     0x82003A84, 0x82003A70, 0x82003A58, 0x82003A3C, 0x82003A20, 0x82003A00};
  for (size_t i = 0; i < 12; ++i) n.reverb_params[i] = image_string(img, reverb_names[i]);
  n.reverb_tables = ReverbTables::from_image(img);
  for (const std::string& e : n.events)
    if (e.empty()) throw FormatError("audio names: event table incomplete");
  return n;
}

AudioEngine::AudioEngine() = default;
AudioEngine::~AudioEngine() = default;

bool AudioEngine::init(const std::filesystem::path& data_dir, std::string* error) {
  try {
    const Bytes image = read_file(data_dir / "xex" / "image.bin");
    const AudioNames names = AudioNames::from_image(image);
    WaveStore waves;
    waves.load(data_dir / "audio");
    const std::filesystem::path dir = data_dir / "files" / "audio";
    auto load_def = [&](const std::string& file, bool required) -> std::unique_ptr<Aobj> {
      const std::filesystem::path p = find_file(dir, file);
      if (p.empty()) {
        if (required) throw FormatError("audio: missing " + file);
        return nullptr;
      }
      return std::make_unique<Aobj>(parse_aobj(read_file(p)));
    };
    const auto system = load_def(names.system_file, true);
    const auto fx = load_def(names.fx_file, false);
    const auto ui = load_def(names.ui_music_file, false);
    std::vector<std::unique_ptr<Aobj>> songs;
    std::vector<const Aobj*> song_ptrs;
    for (int i = 1; i <= 6; ++i) {
      songs.push_back(load_def(with_number(names.song_file, i), false));
      song_ptrs.push_back(songs.back().get());
    }
    load(names, std::move(waves), *system, fx.get(), ui.get(), song_ptrs);
    return true;
  } catch (const std::exception& e) {
    if (error) *error = e.what();
    objects_.clear();
    return false;
  }
}

void AudioEngine::load(const AudioNames& names, WaveStore waves, const Aobj& system, const Aobj* fx,
                       const Aobj* ui_music, std::span<const Aobj* const> songs) {
  std::lock_guard<std::mutex> guard(render_lock_);
  waves_ = std::move(waves);
  objects_.clear();
  songs_.clear();
  song3_object_ = song3_out_ = -1;
  shared_ = SharedState{};

  // sub_820778A0: one object, its ActionIn / ParameterIn handles by name
  // (first match in component order; a missing name leaves the handle
  // empty and the calls skip it).
  auto add = [&](const Aobj& def) -> uint8_t {
    Slot s;
    s.obj = std::make_unique<Object>(shared_, def, &waves_);
    for (size_t i = 0; i < kEventCount; ++i) s.actions[i] = s.obj->find(names.events[i], kFlagActionIn);
    for (size_t i = 0; i < kParamCount; ++i) s.params[i] = s.obj->find(names.params[i], kFlagParameterIn);
    objects_.push_back(std::move(s));
    return uint8_t(objects_.size() - 1);
  };
  // Connects output `out` of object `from` to input `in` of System
  // (sub_82077B88 surround+LFE, sub_82077CF0 mono).
  auto connect = [&](uint8_t from, const std::string& out, const std::string& in) {
    Object& src = *objects_[from].obj;
    Object& sys = *objects_[0].obj;
    const int32_t o = src.find(out, ~0u);
    const int32_t i = sys.find(in, ~0u);
    if (o >= 0 && i >= 0) connect_audio_output(*src.comps[size_t(o)], audio_input_accumulator(*sys.comps[size_t(i)]));
  };

  add(system);  // sub_820799D8 loads System first
  if (fx) connect(add(*fx), names.fx_out, names.fx_in);
  if (ui_music) {
    const uint8_t u = add(*ui_music);
    connect(u, names.ui_out, names.ui_in);
    connect(u, names.mono_out, names.ui_mono_in);
  }
  for (size_t i = 0; i < songs.size() && i < 6; ++i) {
    if (!songs[i]) continue;
    const int n = int(i) + 1;
    const uint8_t s = add(*songs[i]);
    songs_.push_back(s);
    connect(s, with_number(names.song_out, n), with_number(names.song_in, n));
    connect(s, names.mono_out, with_number(names.song_mono_in, n));
    if (n == 3) {
      song3_object_ = s;
      song3_out_ = objects_[s].obj->find(names.transition_switch, kFlagParameterOut);
    }
  }
  // sub_820799D8: System's dry output feeds the renderer (mgr+0x1C).
  const int32_t r = objects_[0].obj->find(names.renderer_out, ~0u);
  if (r >= 0) connect_audio_output(*objects_[0].obj->comps[size_t(r)], &renderer_);
  // The mono WetOutputToRenderer feeds the wet voice (mgr+0x2C, ring 0):
  // the feeder effect, then the I3DL2 reverb (sub_820EC910).
  const int32_t w = objects_[0].obj->find(names.wet_out, ~0u);
  if (w >= 0) connect_audio_output(*objects_[0].obj->comps[size_t(w)], &wet_);
  for (size_t i = 0; i < 12; ++i) {
    reverb_outs_[i] = objects_[0].obj->find(names.reverb_params[i], kFlagParameterOut);
    reverb_values_[i].store(0.0f, std::memory_order_relaxed);
  }
  reverb_tables_ = std::make_unique<ReverbTables>(names.reverb_tables);
  reverb_ = std::make_unique<Reverb>(reverb_tables_.get());
  reverb_last_ = {};
  {
    std::lock_guard<std::mutex> g(reverb_lock_);
    reverb_set_ = false;
  }
  pending_.clear();
  pending_at_ = 0;
}

void AudioEngine::post(const Message& m) {
  std::lock_guard<std::mutex> guard(queue_lock_);
  queue_.push_back(m);
}

void AudioEngine::fire(Event e) {
  const size_t id = size_t(e);
  if (id >= kEventCount) return;
  // FX, UIMusic and the songs, in that order; System is not targeted.
  for (size_t i = 1; i < objects_.size(); ++i)
    if (objects_[i].actions[id] >= 0) post({Message::kFireAction, uint8_t(i), objects_[i].actions[id], 0.0f});
}

void AudioEngine::set_parameter(Param p, float value) {
  const size_t id = size_t(p);
  if (id >= kParamCount || objects_.empty()) return;
  // FX, UIMusic, System, then the songs.
  auto one = [&](size_t i) {
    if (i < objects_.size() && objects_[i].params[id] >= 0)
      post({Message::kSetParameter, uint8_t(i), objects_[i].params[id], value});
  };
  size_t first_song = 1;
  while (first_song < objects_.size() && std::find(songs_.begin(), songs_.end(), first_song) == songs_.end())
    ++first_song;
  for (size_t i = 1; i < first_song; ++i) one(i);
  one(0);
  for (size_t i = first_song; i < objects_.size(); ++i) one(i);
}

void AudioEngine::suspend_songs(bool suspended) {
  for (uint8_t s : songs_) post({Message::kSuspend, s, -1, suspended ? 1.0f : 0.0f});
}

void AudioEngine::reverb_readback() {
  // sub_82079318 (UseDebugSystemParamsInsteadOfSystemDat off): the twelve
  // values as floats, compared with the last ones sent (dat_821D1E78, zero
  // at start; fcmpu, so a NaN always differs); on any change the I3DL2
  // struct with the int fields truncated (fctidz) goes to mgr+0xA8 ->
  // sub_820EBAE0 -> the wet voice's reverb.
  if (objects_.empty()) return;
  std::array<float, 12> v;
  for (size_t i = 0; i < 12; ++i) v[i] = reverb_values_[i].load(std::memory_order_relaxed);
  bool changed = false;
  for (size_t i = 0; i < 12; ++i) changed = changed || !(v[i] == reverb_last_[i]);
  if (!changed) return;
  auto as_int = [](float x) {
    if (x != x) return int32_t(0);  // 0x8000000000000000, low word 0
    if (x >= 9.2233720368547758e18f) return int32_t(-1);
    if (x <= -9.2233720368547758e18f) return int32_t(0);
    return int32_t(uint32_t(uint64_t(int64_t(x))));
  };
  I3dl2 p;
  p.room = as_int(v[0]);
  p.room_hf = as_int(v[1]);
  p.rolloff = v[2];
  p.decay_time = v[3];
  p.decay_hf_ratio = v[4];
  p.reflections = as_int(v[5]);
  p.reflections_delay = v[6];
  p.reverb = as_int(v[7]);
  p.reverb_delay = v[8];
  p.diffusion = v[9];
  p.density = v[10];
  p.hf_reference = v[11];
  {
    std::lock_guard<std::mutex> g(reverb_lock_);
    reverb_pending_ = p;
    reverb_set_ = true;
  }
  reverb_last_ = v;
}

void AudioEngine::run_frame() {
  // sub_820E9A48: drain the queue, update the objects, generate, count.
  {
    std::lock_guard<std::mutex> guard(queue_lock_);
    draining_.swap(queue_);
  }
  for (const Message& m : draining_) {
    Object& o = *objects_[m.object].obj;
    switch (m.kind) {
      case Message::kSetParameter:  // sub_820FBDF0
        parameter_in_set(*o.comps[size_t(m.component)], m.value);
        break;
      case Message::kFireAction: {  // sub_820FC1D8
        const int32_t target = action_in_target(*o.comps[size_t(m.component)]);
        o.start(target);
        break;
      }
      case Message::kSuspend:
        o.suspended = m.value != 0.0f;
        break;
    }
  }
  draining_.clear();

  // ObjectManager::Update (sub_820F62B8): the control updates of every
  // instance that is not suspended, when frame % N == 0.
  for (Slot& s : objects_) {
    Object& o = *s.obj;
    if (o.suspended) continue;
    if (shared_.frame % kUpdatePeriod == 0) o.control_update();
  }
  for (size_t i = 0; i < 12; ++i)
    reverb_values_[i].store(reverb_outs_[i] >= 0 ? parameter_out_value(*objects_[0].obj->comps[size_t(reverb_outs_[i])])
                                                 : 0.0f,
                            std::memory_order_relaxed);
  if (song3_object_ >= 0 && song3_out_ >= 0)
    transition_.store(parameter_out_value(*objects_[size_t(song3_object_)].obj->comps[size_t(song3_out_)]),
                      std::memory_order_relaxed);

  // Generation (sub_820F6368): each active instance in list order prepares
  // its audio inputs and generates its outputs. System comes first, so what
  // the other objects add to its inputs is heard one frame later.
  renderer_.clear();
  renderer_.channels = 6;
  wet_.clear();
  wet_.channels = 1;
  for (Slot& s : objects_)
    if (!s.obj->suspended) s.obj->generate_frame();
  dry_voice(renderer_);

  // The wet voice (Audio.DisableReverb is 0 by default, sub_82176970): the
  // feeder effect, then the reverb, whose pending parameters it applies
  // first (sub_821440E8). Its output channel 0 is added after the dry
  // voice; the original's order of the voices into the mastering buffer
  // is not traced and only changes the rounding (DIFFERENCES.md).
  wet_voice(wet_);
  {
    std::lock_guard<std::mutex> g(reverb_lock_);
    if (reverb_set_) {
      reverb_->set_i3dl2(reverb_pending_);
      reverb_set_ = false;
    }
  }
  reverb_->process(wet_.ch[0].data(), wet_.ch[0].data(), reverb_out_);
  add_wet_to_speakers(reverb_out_.fl, renderer_);
  const size_t base = pending_.size();
  pending_.resize(base + size_t(kFrameSamples) * 6);
  float* out = pending_.data() + base;
  for (int i = 0; i < kFrameSamples; ++i)
    for (int c = 0; c < 6; ++c) out[size_t(i) * 6 + size_t(c)] = renderer_.ch[size_t(c)][size_t(i)];
  ++shared_.frame;
}

void AudioEngine::render(float* out, size_t frames) {
  std::lock_guard<std::mutex> guard(render_lock_);
  size_t done = 0;
  while (done < frames) {
    if (pending_at_ * 1 >= pending_.size()) {
      pending_.clear();
      pending_at_ = 0;
      if (objects_.empty()) {
        std::fill(out + done * 6, out + frames * 6, 0.0f);
        return;
      }
      run_frame();
    }
    const size_t avail = (pending_.size() - pending_at_) / 6;
    const size_t n = std::min(avail, frames - done);
    std::copy_n(pending_.data() + pending_at_, n * 6, out + done * 6);
    pending_at_ += n * 6;
    done += n;
  }
}

}  // namespace geow2::audio

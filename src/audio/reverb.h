#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

#include "audio/graph.h"

namespace geow2::audio {

// The I3DL2 parameters as the game hands them to the wet voice
// (sub_82079318 -> sub_820EBAE0 -> SetEffectParam index 1). The defaults
// are the effect's own (sub_8214A3C0); they are stored, not applied.
struct I3dl2 {
  int32_t room = -10000;
  int32_t room_hf = 0;
  float rolloff = 0.0f;  // not used by the effect
  float decay_time = 1.0f;
  float decay_hf_ratio = 0.5f;
  int32_t reflections = -10000;
  float reflections_delay = 0.02f;
  int32_t reverb = -10000;
  float reverb_delay = 0.04f;
  float diffusion = 100.0f;
  float density = 100.0f;
  float hf_reference = 5000.0f;
};

// The constant block and the parameter tables the set-up reads
// (dat_8201F178 .. dat_8201FE80: T_1FC, T_238, T_2B4, T_2F0, T_370, T_3F0,
// T_470, T_4F0, T_570, T_778, T_980, T_BA0, T_DC0 and their neighbours),
// copied from the decrypted image as words. The set-up addresses them as
// the original does (base address + 4 * index, so an index past a table
// reads its neighbour); an address outside the block reads 0.
struct ReverbTables {
  static constexpr uint32_t kBase = 0x8201F178;
  static constexpr uint32_t kEnd = 0x8201FE80;
  std::array<float, (kEnd - kBase) / 4> words{};

  float at(uint32_t table, uint32_t index) const;
  static ReverbTables from_image(std::span<const uint8_t> image);
};

// The effect's native parameters (sub_82149F90's output, 0x54 bytes).
struct ReverbNative {
  uint32_t reflections_delay = 5, reverb_delay = 5;
  uint32_t position_l = 6, position_r = 6, matrix_l = 0, matrix_r = 0;
  uint32_t diffusion_early = 8, diffusion_late = 8;
  uint32_t low_gain = 8, low_cutoff = 4, high_gain = 8, high_cutoff = 4, rear_delay = 5;
  float room_freq = 5000.0f, room_main = 0.0f, room_hf = 0.0f, reflections_gain = 0.0f, reverb_gain = 0.0f;
  float decay = 1.0f, density = 100.0f, room_size = 100.0f;
};

// sub_82149F90: I3DL2 -> native parameters.
ReverbNative i3dl2_to_native(const I3dl2& p);

// sub_82152500: the "prime" length lookup in dat_82020218 ({1} and the
// 1229 primes up to 9973), a binary search that is not a nearest-prime
// search (10 gives 7).
uint32_t reverb_length(uint32_t x);

// XAudio's I3DL2 reverb as linked into the original (state block of
// sub_821493C0, set-up sub_82148590, frame sub_821429F0).
// The state is allocated once; process() allocates nothing.
class Reverb {
 public:
  explicit Reverb(const ReverbTables* tables = nullptr);

  // sub_8214A268 as sub_821440E8 calls it at the start of a frame.
  void set_i3dl2(const I3dl2& p);
  // sub_82148590.
  void set_native(const ReverbNative& n);

  struct Output {
    std::array<float, kFrameSamples> fl{}, fr{}, bl{}, br{};
  };
  // sub_821429F0 on one 256-sample frame; `l` and `r` are the two input
  // channels (the same for the game's mono voice, S+0x64 == 1).
  void process(const float* l, const float* r, Output& out);

 private:
  using Frame = std::array<float, kFrameSamples + 1>;  // [0] = the stage's carry

  struct Line {  // {u32 pos; fill; buf[N]}
    std::vector<float> buf;
    uint32_t mask = 0, pos = 0;
    void init(uint32_t n);
    float tap(uint32_t len) const { return buf[(pos - len) & mask]; }
    void push(float x) {
      buf[pos & mask] = x;
      pos = (pos + 1) & mask;
    }
  };
  struct Xfade {  // sub_821413F0 / sub_82142480
    float f = 0.0f;
    uint32_t cur = 0, old = 0, target = 0;
    float carry = 0.0f;
    Line line;
    void retarget(uint32_t len);
    void run(const float* in, Frame& out, bool copy_path);
  };
  struct RoomFilter {  // sub_82141A48, coefficients sub_82148130
    float fs = 48000.0f, freq = 24000.0f, room = 0.0f, room_hf = 0.0f;  // +0, +4, +8, +0xC
    float y1 = 0.0f, y2 = 0.0f, g = 1.0f, a = 0.0f;
    void coefficients();
    void run(const Frame& in, Frame& out);
  };
  struct Taps {  // sub_82141B78 / sub_82141ED0
    uint32_t l0 = 0, l1 = 0, l2 = 0;
    float c0 = 0.0f, c1 = 0.0f, c2 = 0.0f, carry1 = 0.0f, carry2 = 0.0f;
    Line line;
    void run(const Frame& in, Frame& out1, Frame& out2);
  };
  struct Taps3 {  // sub_821423D8
    uint32_t l0 = 0, l1 = 1;
    float c0 = 0.0f, c1 = 0.0f, carry1 = 0.0f, carry2 = 0.0f;
    Line line;
    void run(const Frame& in, Frame& out1, Frame& out2);
  };
  struct Allpass {  // sub_82141C48 / sub_82141FA0 / sub_82142270 / sub_82142108
    uint32_t len = 0;
    float neg = 0.0f, c = 0.0f, carry = 0.0f;
    Line line;
    void set(float coef) { c = coef, neg = -coef; }
    float step(float x) {
      const float d = line.tap(len);
      const float v = std::fma(c, d, x);
      line.push(v);
      return std::fma(neg, v, d);
    }
    void run_in_place(Frame& p);
    void run(Frame& in, Frame& out);
  };
  struct LateAllpass {  // the late network's allpasses: len, -c (+4), c (+8), y
    uint32_t len = 0;
    float neg = 0.0f, c = 0.0f, y = 0.0f;
    Line line;
    void set(float coef) { c = coef, neg = -coef; }
  };
  struct LateMix {  // APf / APj: len, k1, k2, k3, y
    uint32_t len = 0;
    float k1 = 0.0f, k2 = 0.0f, k3 = 0.0f, y = 0.0f;
    Line line;
  };
  struct TwoTap {  // DLA..DLD
    uint32_t la = 0, lb = 0;
    float ga = 1.0f, gb = 0.0f, ya = 0.0f, yb = 0.0f;
    Line line;
  };
  struct Biquad {  // the small filters; their tap is always z2 (sel == 1)
    float c = 0.0f, ca = 0.0f, cb = 0.0f, cc = 0.0f, y = 0.0f, z1 = 0.0f, z2 = 0.0f;
  };

  void late(const Frame& f0, const Frame& f1, const Frame& g0, const Frame& g1, const Frame& d0, const Frame& d1);

  const ReverbTables* tables_;
  float fs_ = 48000.0f;  // S+0x58
  float wet_ = 1.0f;     // S+0x5C
  float gr_ = 1.0f;      // S+0x60, the reflections gain

  // Per channel (0 = left chain, 1 = right chain).
  std::array<Xfade, 2> pre_;
  std::array<RoomFilter, 2> rlp_;
  std::array<Taps, 2> t1_, t2_;
  std::array<Allpass, 2> ap1_, ap2_, ap3_;
  std::array<Taps3, 2> t3_;
  float k_r1_ = -0.0125f, k_l2_ = -0.0675f, k_l1_ = -0.0125f, k_r2_ = -0.0675f;  // S+0x200E8/EC, S+0x247B0/B4

  LateAllpass apa_, apb_, apc_, apd_, ape_, apg_, aph_, api_, apk_, apl_;
  LateMix apf_, apj_;
  TwoTap dla_, dlb_, dlc_, dld_;
  Biquad eq1l_, eq2l_, eq3l_, eq1r_, eq2r_, eq3r_, lp1l_, lp1r_, lp2a_, lp2b_;

  std::array<Allpass, 4> oap_;  // S+0x49130, 0x49948 (on OL), 0x4A160, 0x4A978 (on OR)
  std::array<Xfade, 2> rear_;

  // Frame arrays, reused every frame.
  Frame p_, r_, a_[2], b_[2], c_[2], d_[2], e_, f_[2], g_[2], ol_, or_, ql_in_, qr_in_, ql_, qr_;
  std::array<float, kFrameSamples> el_{}, er_{};
};

}  // namespace geow2::audio

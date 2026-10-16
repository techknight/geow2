#pragma once

#include <chrono>
#include <cstdio>
#include <string_view>

namespace geow2 {

// A development aid: named phase timers, on only with --perf. A scope
// adds its wall time to its slot; report() prints each slot's mean and
// worst time per frame over the frames since the last report.
struct PhaseProfile {
  static constexpr int kSlots = 32;
  bool on = false;
  const char* names[kSlots] = {};
  double total_ms[kSlots] = {};
  double frame_ms[kSlots] = {};
  double worst_ms[kSlots] = {};
  int frames = 0;

  int slot(const char* name) {
    for (int i = 0; i < kSlots; ++i) {
      if (!names[i]) {
        names[i] = name;
        return i;
      }
      if (names[i] == name) return i;
    }
    return kSlots - 1;
  }
  void end_frame(int step = -1, double spike_ms = 6.0) {
    // A slow frame: its breakdown, at once.
    double sum = 0.0;
    for (int i = 0; i < kSlots && names[i]; ++i)
      if (std::string_view(names[i]) == "frame (cpu)" || std::string_view(names[i]) == "gpu wait") sum += frame_ms[i];
    if (sum > spike_ms) {
      std::printf("slow frame at step %d (%.2f ms):", step, sum);
      for (int i = 0; i < kSlots && names[i]; ++i)
        if (frame_ms[i] > 0.5) std::printf(" %s %.2f;", names[i], frame_ms[i]);
      std::printf("\n");
    }
    for (int i = 0; i < kSlots && names[i]; ++i) {
      total_ms[i] += frame_ms[i];
      if (frame_ms[i] > worst_ms[i]) worst_ms[i] = frame_ms[i];
      frame_ms[i] = 0.0;
    }
    ++frames;
  }
  void report(const char* label) {
    if (frames == 0) return;
    std::printf("profile %s (%d frames): mean / worst ms per frame\n", label, frames);
    for (int i = 0; i < kSlots && names[i]; ++i) {
      std::printf("  %-22s %8.3f %8.3f\n", names[i], total_ms[i] / frames, worst_ms[i]);
      total_ms[i] = worst_ms[i] = 0.0;
    }
    std::fflush(stdout);
    frames = 0;
  }
};

inline PhaseProfile g_profile;

class ProfileScope {
 public:
  explicit ProfileScope(int slot) : slot_(slot) {
    if (g_profile.on) start_ = std::chrono::steady_clock::now();
  }
  ~ProfileScope() {
    if (!g_profile.on) return;
    const auto d = std::chrono::steady_clock::now() - start_;
    g_profile.frame_ms[slot_] += std::chrono::duration<double, std::milli>(d).count();
  }

 private:
  int slot_;
  std::chrono::steady_clock::time_point start_{};
};

#define GEOW2_PROF_CAT2(a, b) a##b
#define GEOW2_PROF_CAT(a, b) GEOW2_PROF_CAT2(a, b)
#define PROFILE(name)                                                 \
  static const int GEOW2_PROF_CAT(prof_slot_, __LINE__) = ::geow2::g_profile.slot(name); \
  ::geow2::ProfileScope GEOW2_PROF_CAT(prof_scope_, __LINE__)(GEOW2_PROF_CAT(prof_slot_, __LINE__))

}  // namespace geow2

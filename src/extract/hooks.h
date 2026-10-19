#pragma once

#include <functional>
#include <string>

namespace geow2 {

// What the extractor reports and how it runs FFmpeg. Every hook may be
// left empty: lines are then dropped, progress is not reported, and
// commands go through std::system.
struct ExtractHooks {
  std::function<void(const std::string& line)> log;
  // The whole job's progress, 0 to 1.
  std::function<void(double fraction)> progress;
  // Runs a command line (the program and its quoted arguments, possibly
  // with shell redirections) and returns its exit code.
  std::function<int(const std::string& command)> run;

  void say(const std::string& line) const {
    if (log) log(line);
  }
  void step(double fraction) const {
    if (progress) progress(fraction);
  }
  int system(const std::string& command) const;
};

}  // namespace geow2

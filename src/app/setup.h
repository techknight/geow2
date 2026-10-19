#pragma once

#include <filesystem>

namespace geow2 {

// The setup window, shown when the game finds no data folder: the player
// chooses their copy of the game (the XBLA package file, or a folder of its
// extracted files) and the data folder is made at `data_dir` with the
// extractor's own code. Returns true once the folder is there, false when
// the player quits.
//
// For development: `preview` saves the first screen as a PNG and returns;
// `source` skips the file chooser and makes the folder from that package.
struct SetupDev {
  std::filesystem::path preview;
  std::filesystem::path source;
};
bool run_setup(const std::filesystem::path& data_dir, const SetupDev& dev = {});

}  // namespace geow2

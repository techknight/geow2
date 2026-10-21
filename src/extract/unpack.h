#pragma once

#include <filesystem>
#include <map>
#include <string>

#include "extract/bytes.h"
#include "extract/hooks.h"

namespace geow2 {

// The package's files, keyed by package path, from an STFS package file, a
// folder of its extracted files (names matched without regard to case) or
// a data folder made by geow2 (its `files` folder).
std::map<std::string, Bytes> collect_package(const std::filesystem::path& source, const ExtractHooks& hooks = {});

// The number of known files missing from `files` or different from the
// supported release; each is logged ("missing" / "mismatch").
int check_package(const std::map<std::string, Bytes>& files, const ExtractHooks& hooks = {});

// Makes the data folder at `data_dir` from `source`: the known files by
// their known names, the decrypted executable (xex/) and the sound as PCM
// (audio/; copied from a data folder that has all of it, so that one made
// on another computer needs no ffmpeg here). Returns the number of files
// missing or different, in which case nothing is written.
int unpack(const std::filesystem::path& source, const std::filesystem::path& data_dir, const ExtractHooks& hooks = {});

}  // namespace geow2

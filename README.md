# geow2

A native PC port of Geometry Wars: Retro Evolved 2 (Xbox 360, Xbox Live
Arcade), rebuilt from reverse engineering of the original executable and
from the original game data.

**This repository contains no game data.** To build and play it you need
your own copy of the Xbox Live Arcade release (title ID 584108FF). The
port reads that package and extracts what it needs; nothing from the game
is stored here.

Status: early work. Nothing is playable yet.

## What you need

- Your own Geometry Wars: Retro Evolved 2 XBLA package (the STFS `LIVE`
  file in `584108FF/000D0000/`), or a folder holding the files extracted
  from it.
- Windows 10 or 11, Visual Studio 2022 with the C++ workload, CMake 3.28+,
  Ninja.

## Building

From a "x64 Native Tools Command Prompt for VS 2022":

    cmake -G Ninja -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
    cmake --build build
    ctest --test-dir build

Then turn your package into the cache the port reads:

    build\geow2-extract unpack <path to the LIVE package or extracted folder> cache
    build\geow2-extract verify cache

The extractor accepts only the release the port was traced against
(title 584108FF, version 0x102); it checks every file's SHA-1 and every
hash inside `default.xex`.

Before committing, enable the guard that refuses game data:

    git config core.hooksPath tools/hooks

## Differences from the original

The port aims to behave exactly like the Xbox 360 game. Every deliberate
difference is listed in [DIFFERENCES.md](DIFFERENCES.md).

## License

MIT, see [LICENSE](LICENSE). Geometry Wars is a trademark of its owners;
this project is not affiliated with or endorsed by them.

# geow2

A native PC port of Geometry Wars: Retro Evolved 2 (Xbox 360, Xbox Live
Arcade), rebuilt from reverse engineering of the original executable and
from the original game data.

**This repository contains no game data.** To build and play it you need
your own copy of the Xbox Live Arcade release (title ID 584108FF). The
port reads that package and extracts what it needs; nothing from the game
is stored here.

Status: all six single-player modes play (Deadline, King, Evolved,
Pacifism, Waves, Sequence) with their enemies, the grid, particles,
debris, geoms and the multiplier, bombs, lives, the HUD, sound and music,
the front end (menus, rules screens, options, results, unlocking the
modes), local score boards and local achievements. Not in yet: local
multiplayer, Sequence's between-levels screen, Pacifism's edge marks.

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

Then turn your package into the cache the port reads (the sound needs
`ffmpeg` on PATH, or its path in the environment variable `GEOW2_FFMPEG`,
to decode the game's XMA audio; without it the game is partly silent):

    build\geow2-extract unpack <path to the LIVE package or extracted folder> cache
    build\geow2-extract verify cache

The extractor accepts only the release the port was traced against
(title 584108FF, version 0x102); it checks every file's SHA-1 and every
hash inside `default.xex`.

Run the game (it looks for `cache` next to the executable, or pass
`--data`):

    build\geow2 --data cache

Controls: a gamepad (left stick moves, right stick fires, either trigger
sets off a bomb, START pauses; the sticks and bomb buttons can be
swapped in Help and options), or the keyboard: WASD move, the arrow keys
fire (and move through menus), Space bombs, Enter selects, Escape goes
back in menus and pauses a game. "Return to arcade" quits.

Your settings, unlocked modes, local scores and achievements are kept in
your user folder (`%APPDATA%\geow2\geow2` on Windows), or in the folder
given with `--profile`.

The build needs network access the first time (CMake fetches SDL3) and
the Vulkan SDK's `glslc` for the shaders. Configure with
`-DGEOW2_BUILD_GAME=OFF` to build only the extractor and the tests.

Before committing, enable the guard that refuses game data:

    git config core.hooksPath tools/hooks

## Differences from the original

The port aims to behave exactly like the Xbox 360 game. Every deliberate
difference is listed in [DIFFERENCES.md](DIFFERENCES.md).

## License

MIT, see [LICENSE](LICENSE). Geometry Wars is a trademark of its owners;
this project is not affiliated with or endorsed by them.

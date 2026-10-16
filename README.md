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
modes), local score boards and local achievements, and local
multiplayer for up to four players (versus and co-op, teams and
copilots, power-ups).

## What you need

- Your own Geometry Wars: Retro Evolved 2 XBLA package (the STFS `LIVE`
  file in `584108FF/000D0000/`), or a folder holding the files extracted
  from it.
- To build it, one of:
  - Windows 10 or 11: Visual Studio 2022 with the C++ workload, CMake
    3.28+, Ninja, and the Vulkan SDK (for `glslc`).
  - Linux (x86-64): GCC 13+ or Clang 17+, CMake 3.28+, Ninja, `glslc`
    (Debian and Ubuntu: `glslc`; Arch: `shaderc`), and the development
    packages SDL needs for windows, sound and pads. On Debian or Ubuntu:

        sudo apt install build-essential cmake ninja-build glslc libx11-dev \
          libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev \
          libxtst-dev libxkbcommon-dev libwayland-dev libdecor-0-dev \
          libegl-dev libdrm-dev libgbm-dev libasound2-dev libpulse-dev \
          libpipewire-0.3-dev libudev-dev libdbus-1-dev

  - macOS 11+ (Apple Silicon or Intel): the Xcode command line tools and,
    from Homebrew, `cmake ninja shaderc spirv-cross`. The game draws with
    Metal; the build translates its shaders for it.
- `ffmpeg`, once, to turn the game's sound into the data folder (see
  below). The data folder is the same on every system, so you can make
  it on one machine and copy it to another.

## Building

On Windows, from a "x64 Native Tools Command Prompt for VS 2022"; on
Linux and macOS, from a terminal:

    cmake -G Ninja -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
    cmake --build build
    ctest --test-dir build

Then turn your package into the `data` folder the port reads (the sound needs
`ffmpeg` on PATH, or its path in the environment variable `GEOW2_FFMPEG`,
to decode the game's XMA audio; without it the game is partly silent).
The commands are shown for Linux and macOS; on Windows write
`build\geow2-extract` and `build\geow2`:

    build/geow2-extract unpack <path to the LIVE package or extracted folder> data
    build/geow2-extract verify data

The extractor accepts only the release the port was traced against
(title 584108FF, version 0x102); it checks every file's SHA-1 and every
hash inside `default.xex`.

Run the game (it looks for `data` next to the executable, then in your
user folder below, or pass `--data`):

    build/geow2 --data data

Controls: a gamepad (left stick moves, right stick fires, either trigger
sets off a bomb, START pauses; the sticks and bomb buttons can be
swapped in Help and options), or the keyboard: WASD move, the arrow keys
fire (and move through menus), Space bombs, Enter selects, Escape goes
back in menus and pauses a game. "Return to arcade" quits.

Multiplayer takes up to four pads (the keyboard counts as the first):
choose Multiplayer in the main menu, and the others join with A or START.

The game speaks your system's language when it is one the original has
(English, German, French, Spanish, Italian); `--language de` and so on
choose another.

Display: F11 (or Alt+Enter, or Ctrl+Cmd+F on a Mac) toggles fullscreen. `--720p` renders exactly
1280x720 and scales it to the window (`--native` goes back to rendering
at the window's size), `--no-vsync` / `--vsync` choose the presentation;
these choices are remembered in `display.txt`.

Your settings, unlocked modes, local scores and achievements are kept in
your user folder (`%APPDATA%\geow2\geow2` on Windows,
`~/.local/share/geow2/geow2` on Linux, `~/Library/Application
Support/geow2/geow2` on macOS), or in the folder given with `--profile`.
A game pauses when its window is minimised or loses the focus, as the
original does when the console's guide opens.

With vsync on a 60 Hz display the game takes one simulation step per
frame, as the original does (on 120 or 240 Hz, one every second or fourth
frame). At other refresh rates, such as 90 or 144 Hz, it steps from the
clock, which can judder slightly; 60 Hz gives the smoothest picture.

To make a folder you can copy elsewhere (the game, the extractor, the
shaders and these documents; the C runtime and SDL are linked in):

    cmake --install build --prefix dist

then run `dist/geow2-extract unpack <package> dist/data` once and start
`dist/geow2`.

The build needs network access the first time (CMake fetches SDL3) and
the Vulkan SDK's `glslc` for the shaders. Configure with
`-DGEOW2_BUILD_GAME=OFF` to build only the extractor and the tests.

Before committing, enable the guard that refuses game data:

    git config core.hooksPath tools/hooks

## Steam Deck

Either build works on the Deck. The simplest is the Windows build under
Proton; a Linux build runs natively. Make the data folder on your PC
first; the Deck then needs neither `ffmpeg` nor your package.

1. On the PC, make the folder as above (`cmake --install build --prefix
   dist`, then `dist/geow2-extract unpack <package> dist/data`) and copy
   all of `dist` to a USB stick or microSD card.
2. On the Deck, in Desktop Mode, copy the folder to your home folder (for
   example `~/Games/geow2`) rather than running it from the card. A
   Linux build loses its executable mark on a FAT-formatted card: in a
   terminal there, `chmod +x geow2 geow2-extract`.
3. In Steam, "Add a Non-Steam Game" and pick `geow2.exe` (Windows build)
   or `geow2` (Linux build). In the game's Properties, under
   Compatibility, choose a Proton version for the Windows build, or
   "Steam Linux Runtime 3.0 (sniper)" for the Linux build. Optionally add
   the launch option `--profile profile` to keep your saves next to the
   game, where they are easy to copy.
4. The OLED Deck's screen runs at 90 Hz: set the game's refresh rate to
   60 Hz in the performance menu (the "..." button) for the smoothest
   picture.

The Deck's 1280x800 screen shows the 16:9 picture with thin bars above
and below. To update the game later, copy the new program and `shaders`
folder over the old ones; `data` stays.

## Differences from the original

The port aims to behave exactly like the Xbox 360 game. Every deliberate
difference is listed in [DIFFERENCES.md](DIFFERENCES.md).

## License

MIT, see [LICENSE](LICENSE). Geometry Wars is a trademark of its owners;
this project is not affiliated with or endorsed by them.

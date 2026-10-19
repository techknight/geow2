# geow2

[![CI](https://github.com/techknight/geow2/actions/workflows/ci.yml/badge.svg)](https://github.com/techknight/geow2/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/techknight/geow2)](https://github.com/techknight/geow2/releases/latest)

A native port of Geometry Wars: Retro Evolved 2 (Xbox 360, Xbox Live
Arcade, 2008) for Windows, macOS and Linux, rebuilt from reverse
engineering of the original executable. It plays the original's six
modes, its front end and its local multiplayer exactly as the console
does, at any resolution. Website, with clips of every mode:
**[geow2.com](https://geow2.com)**.

**geow2 contains no game data.** It needs your own copy of the Xbox Live
Arcade release (title ID 584108FF). A bundled tool turns that copy into
the data folder the game reads; nothing from the game is in this
repository or in its downloads. geow2 is an unofficial project, not
affiliated with or endorsed by the game's owners (see [Legal](#legal)).

What plays: Deadline, King, Evolved, Pacifism, Waves and Sequence with
all their enemies, the grid, particles, geoms and the multiplier, bombs,
lives, the HUD, sound and music, the menus, rules screens, options and
results, unlocking the modes, local score boards and local achievements
with the original icons, and local multiplayer for up to four players
(versus and co-op, teams and copilots, power-ups).

## Screenshots

<!-- Screenshots are hosted as GitHub attachments, not in the repository:
     they show the game's own artwork, which is never committed. -->
<img width="1920" height="1080" alt="Evolved mode: gravity wells bursting among dozens of enemies on the warped, glowing grid" src="https://github.com/user-attachments/assets/f94de91b-5741-405c-aad8-ce88469dad54" />

<img width="1920" height="1080" alt="Deadline mode: snakes and enemies swarming late in a game, the grid lit orange" src="https://github.com/user-attachments/assets/ea0beb7b-ecd0-4b78-9948-73e089d7cf0f" />

## Getting started

1. Download the archive for your system from
   [Releases](https://github.com/techknight/geow2/releases) and unpack it.
2. Make the data folder from your copy of the game, once. You need your
   XBLA package (the `LIVE` file in `584108FF/000D0000/`, or a folder
   holding the files extracted from it) and
   [FFmpeg](https://ffmpeg.org/download.html) on your PATH (or its path
   in the environment variable `GEOW2_FFMPEG`) to decode the game's
   sound. In a terminal, in the folder you unpacked:

       geow2-extract unpack <your package or extracted folder> data
       geow2-extract verify data

   (On macOS and Linux write `./geow2-extract`.) The tool accepts only
   the release the port was made from (version 0x102) and checks every
   file's SHA-1. The data folder is the same on every system: you can
   make it on one machine and copy it to another.
3. Start `geow2` (`geow2.exe` on Windows, `geow2.app` on macOS).

Where the game looks for `data`: next to the program (next to
`geow2.app` on macOS), then in your user folder (below), or wherever
`--data <folder>` says.

- **Windows**: the programs are signed. Windows may still say it
  "protected your PC" for a while after each release, until the new
  version builds up reputation: choose "More info", then "Run anyway".
- **macOS** (11 or later, Apple Silicon or Intel): the app is signed and
  notarized. FFmpeg for the extraction: `brew install ffmpeg`.
- **Linux** (x86-64): any current distribution with a Vulkan driver.
  `geow2.desktop` and `geow2.png` in the archive can go to
  `~/.local/share/applications` and `~/.local/share/icons`.

The game needs Vulkan on Windows and Linux (any recent graphics driver),
and Metal on macOS.

## Playing

Controls: a gamepad (left stick moves, right stick fires, either trigger
sets off a bomb, START pauses; the sticks and the bomb buttons can be
swapped in Help and options), or the keyboard: WASD moves, the arrow keys
fire (and move through menus), Space bombs, Enter selects, Escape goes
back in menus and pauses a game. "Return to arcade" quits.

Multiplayer takes up to four pads (the keyboard counts as the first):
choose Multiplayer in the main menu, and the others join with A or START.

The game speaks your system's language when it is one the original has
(English, German, French, Spanish, Italian); `--language de` and so on
choose another.

Display: F11 (or Alt+Enter, or Ctrl+Cmd+F on a Mac) toggles fullscreen.
`--720p` renders exactly the original's 1280x720 and scales it to the
window (`--native` goes back to rendering at the window's size);
`--no-vsync` and `--vsync` choose the presentation. These choices are
remembered.

With vsync on a 60 Hz display the game takes one simulation step per
frame, as the original does (on 120 or 240 Hz, one every second or fourth
frame). At other refresh rates, such as 90 or 144 Hz, it steps from the
clock, which can judder slightly; 60 Hz gives the smoothest picture.

A game pauses when its window is minimised or loses the focus, as the
original does when the console's guide opens.

Your settings, unlocked modes, local scores and achievements are kept in
your user folder (`%APPDATA%\geow2\geow2` on Windows,
`~/.local/share/geow2/geow2` on Linux,
`~/Library/Application Support/geow2/geow2` on macOS), or in the folder
given with `--profile`.

## Steam Deck

Either build runs on the Deck: the Windows build under Proton, or the
Linux build natively. Make the data folder on a PC first; the Deck then
needs neither FFmpeg nor your package.

1. Copy the unpacked release folder, with its `data` folder, to a USB
   stick or microSD card.
2. On the Deck, in Desktop Mode, copy the folder to your home folder (for
   example `~/Games/geow2`) rather than running it from the card. A FAT
   or exFAT card drops the Linux build's executable mark: in a terminal
   there, `chmod +x geow2 geow2-extract`.
3. In Steam, "Add a Non-Steam Game" and pick `geow2.exe` (Windows build)
   or `geow2` (Linux build). For the Windows build, choose a Proton
   version in the game's Properties under Compatibility; the Linux build
   needs no compatibility tool. Optionally add the launch option
   `--profile profile` to keep your saves next to the game.
4. The OLED Deck's screen runs at 90 Hz: set the game's refresh rate to
   60 Hz in the performance menu (the "..." button) for the smoothest
   picture.

The Deck's 1280x800 screen shows the 16:9 picture with thin bars above
and below. To update the game, copy the new release over the old one;
`data` stays.

## Building from source

You need CMake 3.28+, Ninja and `glslc`, plus:

- **Windows 10 or 11**: Visual Studio 2022 with the C++ workload, and the
  Vulkan SDK (for `glslc`). Build from an "x64 Native Tools Command
  Prompt for VS 2022".
- **Linux**: GCC 13+ or Clang 17+, `glslc` (Debian and Ubuntu: `glslc`;
  Arch: `shaderc`), and the development packages SDL needs for windows,
  sound and pads. On Debian or Ubuntu:

      sudo apt install build-essential cmake ninja-build glslc libx11-dev \
        libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev \
        libxtst-dev libxkbcommon-dev libwayland-dev libdecor-0-dev \
        libegl-dev libdrm-dev libgbm-dev libasound2-dev libpulse-dev \
        libpipewire-0.3-dev libudev-dev libdbus-1-dev

- **macOS**: the Xcode command line tools and, from Homebrew,
  `cmake ninja shaderc spirv-cross` (the build translates the shaders for
  Metal).

Then:

    cmake -G Ninja -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
    cmake --build build
    ctest --test-dir build
    cmake --install build --prefix dist

`dist` is then a folder like a release's (the C and C++ runtimes and SDL
are linked in). The first configure downloads SDL 3. Configure with
`-DGEOW2_BUILD_GAME=OFF` to build only the extractor and the tests.

## Differences from the original

The port aims to behave exactly like the Xbox 360 game, and its code
cites the original functions and data it follows. Every deliberate
difference (online services, resolution, input devices, and a few
others) is listed, with the evidence, in [DIFFERENCES.md](DIFFERENCES.md).

## Reporting bugs and contributing

Report bugs in [Issues](https://github.com/techknight/geow2/issues), with
your system, graphics card, the version (`geow2 --version`) and the
output of `geow2-extract verify data`. Never attach or link game files,
or screenshots of your data folder. To contribute, see
[CONTRIBUTING.md](CONTRIBUTING.md).

## Legal

geow2 is an independent, unofficial reimplementation. It is not
affiliated with, endorsed or sponsored by Activision, Bizarre Creations
or Microsoft. Geometry Wars is a trademark of Activision Publishing,
Inc.; Xbox, Xbox 360 and Xbox Live are trademarks of Microsoft
Corporation. Their names are used here only to say what geow2 works
with.

geow2 contains none of the game's code, graphics, sound or text, and
cannot be played without your own legally purchased copy of the game.
Please do not share game files, and do not ask for them. The extractor
reads only your own copy: to turn it into files the independently
written game can use, it decrypts the copy's executable with the
publicly known Xbox 360 retail key (as other open-source tools do) and
decodes its sound with FFmpeg on your machine.

geow2's own code is MIT-licensed, see [LICENSE](LICENSE). The programs
include SDL and a few other open-source parts, listed in
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

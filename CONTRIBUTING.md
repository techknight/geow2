# Contributing to geow2

Thank you for helping. geow2 has one aim: to behave exactly like the Xbox
360 game. A few rules keep it that way and keep it legal.

## Never share game files

- Do not commit, attach, link or paste anything from the game: the
  package, the extracted data folder, decoded sound, textures, text,
  disassembly listings or screenshots of them.
- Enable the guard once per clone; it refuses staged files that are, or
  look like, game data:

      git config core.hooksPath tools/hooks

  CI runs the same check over the whole tree (`python tools/guard.py --all`).
- Tests use synthetic data only. A check against the real game is a hash
  or a measured number written in the test, never the file.

## Match the original, and say where it comes from

- Every behaviour, timing, constant and table follows the decrypted
  `default.xex` or the shipped data, and the code cites what it follows
  as `sub_82XXXXXX` (a function) or `dat_82XXXXXX` (a value), in the
  style of the surrounding code. Fused multiply-adds are written with
  the helpers in `src/game/fused.h` where the original uses `fmadds` and
  friends.
- A behaviour that cannot be traced stays out until it can be. Seeing
  something in an emulator is a way to check a reading, not a source: a
  mismatch means the reading is wrong, never that a number should be
  tuned.
- Anything that deliberately differs from the console (a PC feature, a
  limitation) gets an entry in [DIFFERENCES.md](DIFFERENCES.md): what, what
  the original does with the citation, what the port does and why, and
  the date.

## Build and test

See "Building from source" in the [README](README.md). Before a pull
request:

    cmake --build build
    ctest --test-dir build

The simulation must give bit-identical results on every system: the
build disables floating-point contraction (`-ffp-contract=off` off
MSVC) and the code uses the game's own math functions; do not use the C
library's `sin`, `cos`, `atan2` or `pow` in game logic.

## Style

- C++20, the formatting of the surrounding code (2-space indents, lines
  up to about 120 columns), LF line endings.
- Comments explain what the original does and why the code is as it is;
  plans, to-do lists and history belong in issues and commit messages.
- Commit messages: a short summary line, then what changed and the
  evidence (the functions read, what was measured).

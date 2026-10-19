# Changelog

## Unreleased

- Setup inside the game: when there is no data folder, geow2 asks for
  your copy of the game (the package file or an extracted folder) and
  makes the data folder itself, with a progress bar. It works with a
  gamepad, so it can be done on a Steam Deck. `geow2-extract` still
  works as before.
- On Windows the game finds `ffmpeg.exe` next to itself, and on a Mac
  Homebrew's FFmpeg even when started from the Finder.
- Linux: an optional bin/ and share/ install layout (`GEOW2_UNIX_LAYOUT`)
  with a desktop entry, AppStream metainfo and icons under the app ID
  `com.geow2.geow2`, which the window now reports.

## 1.0.0

The first release.

- All six modes of Geometry Wars: Retro Evolved 2 (Deadline, King,
  Evolved, Pacifism, Waves, Sequence) with their enemies, the grid,
  particles, geoms and the multiplier, bombs and lives, the HUD, and the
  original's sound engine, music and reverb.
- The front end: menus, rules screens, help and options, results,
  unlocking the modes, local score boards and local achievements with
  the original icons.
- Local multiplayer for up to four players: versus and co-op, teams and
  copilots, power-ups.
- Windows, macOS (Apple Silicon and Intel) and Linux, including the
  Steam Deck; any resolution, or the original's exact 1280x720 frame;
  gamepads and keyboard.
- `geow2-extract` turns your own copy of the game into the data folder.

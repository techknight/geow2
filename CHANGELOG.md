# Changelog

## Unreleased

- Online scores, optional: with a score server for your group of friends
  ([geow2-scores](https://github.com/techknight/geow2-scores), free to
  run on Cloudflare), your high scores, unlocked modes and achievements
  follow you between your devices, the leaderboards show the group's
  boards (the original's Friends and My score views, X to switch), and
  your name is the one you chose. Help and options, Online scores: enter
  the server's address and pair the device by scanning a QR code with
  your phone. `--online-pair` and `--online-sync` do the same from a
  terminal.
- Every finished game is kept in `scores.log`, so progress from two
  devices can be combined: `geow2 --merge-profile <the other device's
  profile folder>` adds its games, unlocks and achievements.
- The main menu's last item reads "Quit" (the game's own word for it, in
  every language) in place of "Return to arcade".
- After a game, the mode select menu keeps the mode just played selected,
  as the original does, rather than going back to Deadline.
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

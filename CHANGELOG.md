# Changelog

## Unreleased

- Online scores: the group's boards from the last sync are kept, so a
  game started without a connection (a Steam Deck away from Wi-Fi, say)
  still shows your friends' scores. They are only shown, never sent or
  added to your games, and are deleted when the device is unpaired.
  `--online-sync` lists them when the server can't be reached.

## 1.2.4

- Controls: the controller picture is smaller so its labels clear the
  panel above the fourth row, and the list no longer widens when the
  Pacifism stick is set to Right stick.

## 1.2.2

- Help and options, Controls: "In Pacifism, move using: Left stick /
  Right stick". With the right stick, Pacifism's ship moves with the
  right stick (the game has no firing there); the other modes keep the
  Movement and Firing choices.
- The Settings item is now called "Smooth motion above 60 Hz".

## 1.2.1

- Setup accepts a data folder made on another computer (or the folder
  around it): it checks the files again and copies the sound, so FFmpeg
  is not needed. Before, it reported the data folder as "not the
  supported release".

## 1.2.0

- Smooth motion on displays faster than 60 Hz: on a 90 Hz OLED Steam
  Deck or a 120 or 144 Hz monitor, the frames between the game's 60
  steps a second are drawn in between (the camera, the ships, enemies
  and shots, the grid, particles and debris) instead of showing each
  step twice or unevenly. The game itself still runs at the original's
  60 steps a second and is unchanged; the picture is a few milliseconds
  behind. At 60 Hz nothing changes. It is on by default: Help and
  options, Settings, Smooth motion turns it off (or `--no-smooth`), for
  the original's 60 pictures a second. The OLED Deck no longer needs its
  refresh rate set to 60 Hz.

## 1.1.4

- The original's hidden debug camera: BACK + RB on the first controller
  (View + R1 on a Steam Deck) freezes the game and lets the sticks fly
  the view around the playfield (the left stick turns, the right stick
  and the triggers move, RB faster, LB slower); press them again to go
  back to the game.
- Shots and bomb blasts still under way when their ship is destroyed no
  longer score, and geoms on their way to it look for another ship, as
  in the original.
- Pausing no longer leaves the background's lighting changing under the
  pause menu.
- Geoms choose between equally near ships, and the debris and the
  numbers left by kills update, in the original's order.
- The Online scores screen shows the game's version at the bottom right.
- For comparing with recordings of the original: `--audio-rng` and
  `--audio-steps` start a headless run's sound from a recorded game's
  values, and `--press` takes the shoulders, the triggers and the
  sticks.

## 1.1.3

- Online scores: a long history of games goes to the server in parts, so
  none is lost, and games that came from the server aren't sent back.
- Online scores: sturdier against a broken or hostile score server (names
  cleaned of control characters, answers limited in size, no redirects, a
  pairing link that can't be shown as a QR code no longer crashes the
  game), and the file holding the device's token is private on Linux and
  macOS.
- Leaderboards as in the original: a cursor that up and down move through
  pages of ten, shoulders and triggers (or the right stick) turning
  pages, the rows' shading and positions; Y closes them when opened from
  Single Player. On the game over screen the friends board scrolls with
  the right stick.
- Online scores: the score at the top right in a game follows Help and
  options, Score display, as on Xbox Live: Next Friend shows the score of
  the next person in your group to beat, Personal Best your own best.
- A damaged sound file in the data folder is refused instead of ending
  the game.

## 1.1.2

- Online scores: pasting the server's address (Ctrl+V, or Cmd+V on a
  Mac) replaces what the field holds, and the screen says how to paste.
- Online scores: the Single Player tiles show the group's best six for
  each mode around your own row, as the original showed your friends'
  (the right stick and the shoulder buttons scroll them).

## 1.1.1

- Install on the Steam Deck and Linux with Flatpak, from
  [geow2.com/flatpak](https://geow2.com/flatpak/): one install, updates
  through Discover or `flatpak update`, and the setup inside the game
  makes the data folder from your copy of the game. Each release also
  carries the Flatpak as a single file.

## 1.1.0

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

# Differences from the Xbox 360 original

Every deliberate departure from the original game's behaviour is recorded
here with the reason and the evidence for what the original does. If it
is not in this file, the port is meant to match the Xbox 360 exactly.

Format for each entry:

- **What**: the behaviour that differs.
- **Original**: what the Xbox 360 build does, with the source (decompiled
  function, data file, or a capture of the original running in an
  emulator) that shows it.
- **Port**: what we do instead and why.
- **Decided**: date.

## Entries

### Xbox Live leaderboards and achievements

- **What**: online leaderboards, friend scores and achievements.
- **Original**: Xbox Live leaderboards per mode (stats views 0..5, one
  "Score" column, written at each game over by `sub_8207E490`), the
  friends board on the mode-select tiles and the results screen
  (`sub_82088AE0`), the leaderboard viewer with its Friends / My score /
  Overall filters (`sub_8208AAB8`), and 12 achievements awarded by
  `sub_82034CA0` (icons `ach_*.png` in the package). The session's own
  score tables (`sub_82034B48`, last game, best this session, previous
  games) are never saved.
- **Port**: a local board per mode keeps the best 100 scores played on
  this machine (saved with the profile). It is shown wherever the
  original shows a Live board: the results screen's right-hand list (the
  rows around the last game's, which is highlighted), the mode-select
  tiles' best score, and the leaderboard screens (one "Overall" view of
  ten rows, no gamer cards; the right stick, LB/LT and RB/RT scroll as
  the original's friends board does, `sub_82089650`, and up and down
  also move one row). The session tables behave as in the
  original. Achievements are unlocked and listed locally with the
  original icons. There is no online service.
- **Decided**: 2026-10-14; local boards' shape 2026-10-15.

### The achievements list

- **What**: where the achievements are shown.
- **Original**: the menus' Achievements item opens the console's own list
  (`XamShowAchievementsUI`, `sub_8208F1C8`); unlocking shows the
  dashboard's notification.
- **Port**: the item opens a screen drawn with the game's UI: the twelve
  achievements in the order of the title's achievement table (its XDBF
  resource in the user's XEX), each with its icon, name, description and
  gamerscore, dimmed until unlocked; up and down scroll it. No
  notification is shown when one unlocks (sound event 0x24 is the
  game's own cue).
- **Decided**: 2026-10-15.

### Online scores

- **What**: sharing scores, unlocked modes and achievements between
  devices and with friends.
- **Original**: Xbox Live keeps the gamer profile (its settings block,
  profile setting 0x63E83FFF, `sub_82081848`) and the achievements with
  the gamertag on every console, and writes each game's score to the
  per-mode Live leaderboards (`sub_8207E490`), which the game shows with
  Friends / My score / Overall filters (`sub_8208AAB8`) and as the friends
  board on the mode-select tiles and the results screen (`sub_82088AE0`).
- **Port**: optional and off until set up, through a self-hosted score
  server (geow2-scores, one per group of friends). Help and options gains
  an Online scores item, out of a game only, with a screen of its own:
  the server's address, typed once, and "Pair this device", which shows a
  code and a QR code to confirm on a phone signed in to the server. Its
  text is English, as the original has none for it. Once paired:
  - the player's name is the one chosen on the server (in place of
    "Player 1");
  - when the game starts, after each single-player game and every five
    minutes in the menus, the finished games (`scores.log`), the unlocked
    modes, the unlock progress and the achievements merge both ways with
    the server: games add up without repeats, bits are combined, progress
    takes the larger value. A device's games, unlocks and achievements
    therefore reach every other device paired to the same player;
  - the group's board (each player's best per mode, the player's own row
    counting this profile's best even before the server has it) stands in
    for the Live boards:
    - the leaderboard screens start on Friends (0x29) and X steps
      Friends, My score (0x2A, opening on the page with the player's row)
      and Overall (0x2B), as `sub_8208AAB8` does. Friends and My score show
      the group's board, Overall the local board, with the original's
      cursor and paging (`sub_8208AB80`); My score opens on the page of
      ten that holds the player's row, the cursor on it (Live returned the
      page around the player). A, View Gamer Card, has no counterpart and
      its tip is left out; the tips gain X Change Filter (0x31). An empty
      group board shows 0x7E.
    - the results screen's friends board scrolls with the right stick and
      the shoulders and triggers (`sub_82089650`).
    - the results screen's Friends Leaderboard column shows the group's
      board, eight rows from the player's row less four, the player's row
      highlighted, names and scores without ranks (`sub_82088AE0`).
    - the mode select tiles show six rows of the group's board as the Live
      mini table (`sub_8208DD58`), from the player's row less three, the
      player's row highlighted (with "-" when they have no score in the
      mode, as a friend without a score shows on Live), scrolled with the
      right stick and LB/LT, RB/RT (`sub_8208D918`).
    - the HUD's top-right score follows the options' Score display as on
      Live (`sub_8207CA48`): Next Friend shows the nearest group member
      above whose score this game hasn't passed, under their name;
      Personal Best the player's best on the group's board, under their
      name. Unpaired, it is the session's best labelled Highscore, as the
      original offline.

  Nothing is sent before a device is paired, and an unpaired game never
  connects; unpaired, the screens are as in "Xbox Live leaderboards and
  achievements".
- **Decided**: 2026-10-19.

### Setup

- **What**: making the data folder.
- **Original**: none (the game runs from its own package).
- **Port**: when no data folder is found, the game opens a setup window
  of its own (not the game's UI: SDL's 2D renderer and debug font, since
  the game's font is in the data it is about to make). The player chooses
  the package or an extracted folder with the system's file chooser, and
  the extractor's code makes the data folder in the user folder, with a
  progress bar. `geow2-extract` does the same from a terminal.
- **Decided**: 2026-10-19.

### Profiles, sign-in and player names

- **What**: who is playing and where their settings are kept.
- **Original**: each pad's signed-in gamer profile; the settings and
  unlock progress are a 48-byte block saved as profile setting
  0x63E83FFF (`sub_82081848`); names shown are gamertags; menus ask for a
  sign-in when a pad has none (`XamShowSigninUI`); a profile change in
  game ends it (`sub_8207DD78`).
- **Port**: one local profile, the same 48-byte block saved as
  `profile.bin` in the user's preferences folder (or `--profile`); beside
  it every finished single-player game in `scores.log`, whose best games
  per mode are the local score boards (also written as `scores.txt`, the
  boards' file before 1.1). `--merge-profile` adds another device's
  profile folder to this one. Every pad counts as
  signed in; the player's name is the original's default for a player
  without a gamertag, text 0x82 with the pad number ("Player 1"), or the
  name chosen on a score server once paired ("Online scores"). In
  multiplayer every pad shows its "Player N" name (as the original does
  for pads without a profile), and all pads use the one profile's stick
  and bomb settings where the original reads each pad's own profile
  (`sub_8204FF00`).
  The console's profile is signed; `profile.bin` is not, so a damaged file
  is repaired on load: settings outside the ranges their menus allow (the
  volumes 0 to 1, the stick bits, the score display 0 or 1) go back to
  the defaults of `sub_82093230` and Deadline stays unlocked. The files
  are written beside and renamed over, never truncated in place.
- **Decided**: 2026-10-15; multiplayer 2026-10-15; repair 2026-10-16.

### Language

- **What**: how the language is chosen.
- **Original**: the console's language (`sub_820829D0`): German, French,
  Spanish and Italian are used as they are, any other is English; the
  text comes from `Text-<code>.loc`, the achievements from the SPA's
  string table for that language, and number separators follow it
  (`sub_8205D740`).
- **Port**: the first of the system's preferred languages that the game
  has, else English; `--language en|de|fr|es|it` overrides it. Headless
  captures use English unless told otherwise, so they do not depend on
  the machine.
- **Decided**: 2026-10-15.

### Leaving the game

- **What**: "Return to arcade".
- **Original**: the main menu's last item, text 0x07 "Return to arcade";
  after its confirmation, `XamLoaderLaunchTitle(NULL, 0x100)` returns to
  the dashboard's arcade (`sub_8208F240`).
- **Port**: the program quits, and the item is labelled with the game's
  own word for that action, its key tip text 0x37 "Quit" (Beenden,
  Quitter, Salir, Abbandona), since there is no arcade to return to.
  The confirmation is the original's (0x73).
- **Decided**: 2026-10-15; the label 2026-10-19.

### Input devices

- **What**: keyboard control and non-Xbox pads.
- **Original**: Xbox 360 pads only, read through `XamInputGetState`; the
  left stick's raw value is mapped as (raw - 0.5) / 32767.5 with Y
  inverted (`sub_82037EA0`), then a 0.25 dead zone (`sub_8204EE28`).
- **Port**: any pad SDL supports (up to four), with SDL's Y axes turned
  into the XInput convention so the same mapping applies, and the
  profile's stick and bomb choices as in the original. Keyboard, as the
  first pad: WASD give full deflection on the left stick, the arrow keys
  on the right stick and the D-pad (so they also move through menus),
  Space a fully pulled right trigger (the bomb), Enter is A, X and Y are
  X and Y, and Escape is START while playing (pause) and B in the menus.
  SDL's triggers (0 to 32767) are scaled to the XInput byte the original
  reads (axes 8 and 9, times 1/255). In multiplayer the keyboard is part
  of the first pad slot (with a pad there, both work). A pad pulled from
  any slot counts as disconnected for one step, so the game pauses as the
  original does for a lost pad (`sub_820336D8`) even though the keyboard,
  or a spare fifth pad taking the slot, keeps it connected afterwards.
- **Decided**: 2026-10-15; menus 2026-10-15; multiplayer 2026-10-15;
  pulled pads 2026-10-16.

### The system UI pause

- **What**: what pauses a game besides START and a lost pad.
- **Original**: a game pauses while the system UI (the guide) shows:
  `sub_820336D8` tests `sub_8207E228`, the flag set by XNotify message 9
  (`XN_SYS_UI`).
- **Port**: a window that is minimised, hidden, covered or loses the
  focus stands in for the system UI and pauses the game the same way
  (the Steam overlay counts when it takes the focus). The game stays
  paused when the window comes back, as after the guide.
- **Decided**: 2026-10-16.

### Resolution

- **What**: output resolution and aspect handling.
- **Original**: renders at 1280x720, or 1920x1080 when the console's
  video mode is high definition (`sub_8205D538`, `XGetVideoMode`
  fIsHiDef); the bloom chain then starts with one more halving step
  (`sub_82064640`).
- **Port**: renders the 16:9 picture at the window or monitor resolution
  (letterboxed). The bloom chain halves from that size as the original
  does from its own, with the extra step whenever the picture is taller
  than 720 lines; at exactly 1280x720 and 1920x1080 it matches the
  original's two layouts, at other sizes the glow's reach in screen terms
  differs slightly. `--720p` instead renders exactly the original's
  1280x720 frame and scales it (bilinear) into the window. Fullscreen
  (F11) and vsync are the player's choice. The simulation always runs at
  the original's tick rate and is never interpolated.
- **Decided**: 2026-10-14; bloom sizes, 720p mode 2026-10-15.

### Frame timing

- **What**: what happens when a frame takes too long, and the 50 Hz
  variant.
- **Original**: one simulation step of a fixed 1/60 s per presented
  frame (`sub_820366A0` adds `dat_8219D280` each frame), presented every
  vblank (present parameters from `sub_8205D538`), so a frame that misses
  a vblank slows the game down rather than skipping time. On a PAL 50 Hz
  console the step is 1/50 s and the game runs at 50 steps a second
  (`sub_8205D538`, `XGetVideoMode`).
- **Port**: with vsync on a display at about 60 Hz or a whole multiple of
  it (59.94, 59.95, 120, 240 Hz), one step every refresh, or every 2, 4,
  ... refreshes, as on the console: a slow frame slows the game. On other
  displays (144 Hz, say) or with vsync off, steps are taken from real time, 60 a second, with frame times
  snapped to the refresh period; after a slow frame the missed steps are
  run before the next picture (at most 8 at once). There is no 50 Hz mode.
- **Decided**: 2026-10-15; locking to the refresh 2026-10-16.

### Reciprocal estimates

- **What**: line widths, grid spring lengths, particle forces, the
  geoms' pull, the snake's segments and shots bouncing off gates use an
  exact 1/x and 1/sqrt.
- **Original**: the line quad emitter (`sub_8205EB58`), the grid
  simulation (`sub_8205B880`), the wall quads (`sub_8205CC70`), the
  particle step (`sub_82063448`), the geom pull (`sub_8204D410`) and the
  snake segments' follow (`sub_82056CD8`) and the gate beams' bounce
  (`sub_82051B18`) use
  `frsqrte` and `fres`, the PowerPC hardware estimates, whose exact
  results on the Xbox 360 CPU are not documented.
- **Port**: computes the exact values. Lines, the grid and particles can
  differ from the original by the estimates' error (small; visual only).
  The geom pull is gameplay: a geom being pulled in moves by the pull
  direction, whose length the estimate leaves within about 1/4096 of 1,
  so a geom can reach the player a step earlier or later than on the
  console. Snake segments trail their leader by a distance and pull read
  from the estimates, so their positions differ from the console's in
  the low bits, as do shots after bouncing off a gate's beam.
  In recorded King games, where geoms are pulled all the time, the
  emulated original picked a geom up a step before the port in two
  games out of two (changing one kill's points, or the game from
  there). The emulator rounds some fused operations differently from
  the console itself, so not every such step is this deviation.
- **Decided**: 2026-10-15.

### Render target precision

- **What**: the precision of the high-range layers and bloom textures.
- **Original**: draws into 7e3 floating-point targets (10 bits per
  channel, maximum 31.875) and resolves them into 10-bit or 16-bit fixed
  point textures with an exponent bias of -5, read back with +5
  (`sub_82065270`, `sub_820653C8`, `sub_8206A3D0`).
- **Port**: uses 16-bit floating-point targets and textures throughout,
  capping every read at 31.875 as the original's storage does. Rounding
  of faint values differs slightly.
- **Decided**: 2026-10-15.

### Display gamma

- **What**: the output gamma ramp the picture passes through.
- **Original**: the Direct3D runtime asks the system which display is
  attached (`VdGetCurrentDisplayGamma`: sRGB, BT.709 or a power) and sets
  the display hardware's ramp to re-encode the image, taken as sRGB, for
  that display (`sub_820AAE20`, `sub_820AAFB0`, set at device creation by
  `sub_820AE1A0`; the game sets no ramp of its own). Captures of the original
  in an emulator (Xenia, which reports an HDTV) match this ramp.
- **Port**: applies the same ramp at the end of the composite, built for
  BT.709 (a high-definition TV, the usual connection), since a PC reports
  no such setting. The display type is a renderer setting.
- **Decided**: 2026-10-15.

### Light map timing

- **What**: when the grid's light map is updated.
- **Original**: once per rendered frame (`sub_82065930`), which is once
  per simulation step since the game renders one frame per step.
- **Port**: once per simulation step, whatever the display's refresh
  rate, so its feedback fades at the original's speed.
- **Decided**: 2026-10-15.

### Reverb

- **What**: the details of the music's reverb that are not reproduced.
- **Original**: the System object's mono `WetOutputToRenderer` feeds a
  mono XAudio voice: the game's effect on it (`sub_820FD630`) halves the
  frame and mutes it above 100, on a non-finite sample or above an RMS of
  5, then XAudio's I3DL2 reverb (`sub_821440E8`, `sub_821429F0`) runs with
  the parameters `sub_82079318` reads from System each game frame and
  sends through `sub_820EBAE0` when one changes. Only the reverb's output
  channel 0 reaches the speakers, added to FL, FR, FL, FR at 1.0 (map
  built by `sub_820EC910`).
- **Port**: the same chain (`src/audio/reverb.cpp`), with these
  differences:
  - The order in which XAudio adds the dry voice, the wet voice and the
    submix into the mastering buffer (and its clearing) is not traced:
    the port starts from the dry voice and adds the wet voice after. Only
    the float rounding of the sum depends on it.
  - The set-up's `pow`, `log`, `cos` and the single-precision square root
    of a double are the host's, not the console runtime's; a coefficient
    can differ in its last bit.
  - VMX denormal flushing (feeder, output mix, mastering adds) is not
    reproduced.
  - The original reads the twelve ParameterOuts from the game thread; the
    port's game thread reads the values the audio thread published after
    its last control update. The parameters take effect at the start of
    the next audio frame in both.
  - Only the first wet voice is built, the one System's wet output and
    the parameters reach (ring 0, index 0). The original can create a
    second and third (as many as manager `+0xFBB8`, whose setter is not
    traced), each with its own reverb that never gets parameters; what
    their rings 1 and 2 carry is not traced, so they are left out.
  - `Audio.DisableReverb` (a development variable, 0 by default,
    `sub_82176970`) is not exposed: the reverb is always on.
  - Not ported because they cannot be traced: what the renderer's 1-, 2-
    and 5-channel rings carry into the dry voice (nothing in the port
    writes them), and the stereo voice `+0x22C` with the type-5 effect on
    its submix `+0x230`.
- **Decided**: 2026-10-16.

### Wave playback

- **What**: how the waves start, loop and end.
- **Original**: the XMA hardware decodes with some latency, loops by bit
  offsets within the wave and drops a final partial 1024-sample buffer
  (`sub_820F9C58`, `sub_820F90B0`).
- **Port**: the extractor's decoded PCM plays through the traced XMA
  player logic: it starts at once, loops the whole decoded wave and
  reports a one-shot as finished on the render after its data ends.
- **Decided**: 2026-10-15.

### Speaker layout

- **What**: the output channels.
- **Original**: the mix is 5.1 floats at 48 kHz handed to XAudio
  (`sub_820EC910`).
- **Port**: the same 5.1 mix goes to SDL as six float channels and SDL
  folds it down to the device's layout (stereo on most PCs).
- **Decided**: 2026-10-15 (user).

### Hardware estimates in the audio

- **What**: the rounding of a few audio control values.
- **Original**: the audio code uses `fres` / `frsqrte` estimates and VMX
  dot products whose rounding is the hardware's.
- **Port**: exact division, square roots and sums; results can differ in
  the last bits.
- **Decided**: 2026-10-15.

### Undefined reads in the audio graphs

- **What**: three reads outside their data.
- **Original**: the bypass loop's wrap can read the sample before a
  wave; the Array blend reads past its table; the Selector walk can run
  past its last entry (whatever memory holds there).
- **Port**: 0, 0, and the last entry respectively.
- **Decided**: 2026-10-15.

### Audio decoding

- **What**: how the compressed sound is decoded.
- **Original**: the sound banks hold XMA1 audio, decoded by the Xbox
  360's audio hardware at play time (`XMACreateContext`).
- **Port**: `geow2-extract` decodes every wave once, with FFmpeg's `xma1`
  decoder, to 16-bit PCM in the data folder (as does the setup window,
  with the same code); the game plays the PCM. FFmpeg is needed only at
  extraction. The samples can differ from the console's decoder in
  rounding, and between FFmpeg builds by at most one step in a few waves.
- **Decided**: 2026-10-15.

### Particle pool limit

- **What**: when the particle pool counts as full, which makes an
  explosion skip its particles (and its random draws).
- **Original**: 256 blocks of 128 particles. New particles only go into
  the current block, then the next free one (`sub_82063100`); "full"
  means the current block is the last and holds 128 (`sub_82062FC8`).
  The step runs on six worker threads that claim blocks through an
  atomic counter and each compact their survivors into their own blocks
  (`sub_82063A38`); the list is then sorted and the current block set to
  the last used one (`sub_82064110`, `sub_82062F28`). Partly filled
  blocks before the current one are never refilled, so the real limit
  lies between about 32,100 and 32,768 particles and depends on how the
  threads were scheduled.
- **Port**: full at 32,768 live particles. Below about 32,100 the two
  agree exactly. An explosion skipped for a full pool also skips its
  random draws, so a game that fills the pool (many enemies destroyed at
  once, as when the ship dies in a crowded Sequence level) goes on
  differently from there, as it would between two runs on the console.
  Measured against the emulated original in two Sequence games: at the
  ship's death among 65-76 enemies the port's pool took about 120-160
  particles more before counting as full (146 and 200 more random
  draws); everything before matched exactly.
- **Decided**: 2026-10-15.

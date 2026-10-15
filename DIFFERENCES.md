# Differences from the Xbox 360 original

Every deliberate departure from the original game's behaviour is recorded
here with the reason and the evidence for what the original does. If it
is not in this file, the port is meant to match the Xbox 360 exactly.

Format for each entry:

- **What**: the behaviour that differs.
- **Original**: what the Xbox 360 build does, with the source (decompiled
  function, data file, or oracle capture) that shows it.
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
  tiles' best score, and the leaderboard screens (one "Overall" view; up
  and down scroll, no gamer cards). The session tables behave as in the
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

### Profiles, sign-in and player names

- **What**: who is playing and where their settings are kept.
- **Original**: each pad's signed-in gamer profile; the settings and
  unlock progress are a 48-byte block saved as profile setting
  0x63E83FFF (`sub_82081848`); names shown are gamertags; menus ask for a
  sign-in when a pad has none (`XamShowSigninUI`); a profile change in
  game ends it (`sub_8207DD78`).
- **Port**: one local profile, the same 48-byte block saved as
  `profile.bin` in the user's preferences folder (or `--profile`); the
  local score boards beside it as `scores.txt`. Every pad counts as
  signed in; the player's name is the original's default for a player
  without a gamertag, text 0x82 with the pad number ("Player 1").
- **Decided**: 2026-10-15.

### Leaving the game

- **What**: "Return to arcade".
- **Original**: after its confirmation, `XamLoaderLaunchTitle(NULL,
  0x100)` returns to the dashboard's arcade (`sub_8208F240`).
- **Port**: the program quits.
- **Decided**: 2026-10-15.

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
  reads (axes 8 and 9, times 1/255).
- **Decided**: 2026-10-15; menus 2026-10-15.

### Resolution

- **What**: output resolution and aspect handling.
- **Original**: renders at 1280x720, or 1920x1080 when the console's
  video mode is high definition (`sub_8205D538`, `XGetVideoMode`
  fIsHiDef); the bloom chain then starts with one more halving step
  (`sub_82064640`).
- **Port**: renders the 16:9 picture at the window or monitor resolution
  (letterboxed), with an option for an exact 720p internal render. The
  bloom chain halves from that size as the original does from its own,
  with the extra step whenever the picture is taller than 720 lines; at
  exactly 1280x720 and 1920x1080 it matches the original's two layouts,
  at other sizes the glow's reach in screen terms differs slightly. The
  simulation always runs at the original's tick rate and is never
  interpolated.
- **Decided**: 2026-10-14; bloom sizes 2026-10-15.

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
  `sub_820AE1A0`; the game sets no ramp of its own). Oracle captures
  (Xenia, which reports an HDTV) match this ramp.
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

- **What**: the music's and effects' reverb.
- **Original**: the System object's mono wet output feeds an XAudio
  I3DL2 reverb voice whose parameters follow the song (`sub_82078C88`,
  `sub_82079318`, `sub_820EBAE0`).
- **Port**: the wet send is computed and dropped: there is no reverb.
  The reverb's internals are XAudio's own, not in the traced code.
- **Decided**: 2026-10-15 (until the reverb can be traced).

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
  decoder, to 16-bit PCM in the cache; the game plays the PCM. FFmpeg is
  needed only at extraction. The samples can differ from the console's
  decoder in rounding.
- **Decided**: 2026-10-15 (FFmpeg at extraction time, with the user).

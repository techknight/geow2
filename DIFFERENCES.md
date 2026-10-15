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
- **Original**: Xbox Live leaderboards per mode, friend scores shown in
  game, 12 achievements (icons `ach_*.png` in the package). Exact call
  sites to be cited once traced.
- **Port**: local per-mode high-score tables laid out like the original
  ones; achievements are unlocked and listed locally with the original
  icons. There is no online service.
- **Decided**: 2026-10-14.

### Input devices

- **What**: keyboard control and non-Xbox pads.
- **Original**: Xbox 360 pads only, read through `XamInputGetState`; the
  left stick's raw value is mapped as (raw - 0.5) / 32767.5 with Y
  inverted (`sub_82037EA0`), then a 0.25 dead zone (`sub_8204EE28`).
- **Port**: any pad SDL supports, with SDL's Y axes turned into the
  XInput convention so the same mapping applies (left stick moves, right
  stick fires, the original's default). Keyboard: WASD give full
  deflection on the move stick, the arrow keys on the fire stick (bomb
  binding to come with that feature).
- **Decided**: 2026-10-15.

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

### Reciprocal square root estimates

- **What**: line widths and grid spring lengths use an exact 1/sqrt.
- **Original**: the line quad emitter (`sub_8205EB58`) and the grid
  simulation (`sub_8205B880`) normalise with `frsqrte`, the PowerPC
  hardware estimate, whose exact results on the Xbox 360 CPU are not
  documented.
- **Port**: computes the exact reciprocal square root in double. Lines
  and the grid can differ from the original by the estimate's error
  (small; visual only, gameplay never reads these values).
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

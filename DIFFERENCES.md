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
- **Port**: any pad SDL supports, with SDL's Y axis turned into the
  XInput convention so the same mapping applies. Keyboard: WASD or the
  arrow keys give full deflection on the move stick (fire and bomb
  bindings to come with those features).
- **Decided**: 2026-10-15.

### Resolution

- **What**: output resolution and aspect handling.
- **Original**: renders for a 1280x720 output (to be confirmed from the
  render-target setup).
- **Port**: renders at the window or monitor resolution, with an option
  for an exact 720p internal render. The simulation always runs at the
  original's tick rate and is never interpolated.
- **Decided**: 2026-10-14.

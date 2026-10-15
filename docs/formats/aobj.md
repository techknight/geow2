# Audio objects (`audio/*.dat`, magic `AOBJ`)

Everything the game plays is decided by nine audio objects: the system
mix (`System.dat`), the sound effects (`GW3_FX.dat`), the menu music
(`UIMusic.dat`) and one per song (`Song_1.dat` .. `Song_6.dat`). Each is
a graph of components (inputs, scripts, timers, curves, sounds, mixers,
filters) that the engine runs as data: the game only fires named events
and sets named parameters. Big endian. Implemented in
`src/audio/aobj.cpp`; the components are in `src/audio/`.

## Header (0x14 bytes)

| offset | size | field |
|---|---|---|
| 0x00 | 4 | magic `AOBJ` |
| 0x04 | 4 | differs per file; nothing reads it |
| 0x08 | 4 | file offset of the string table |
| 0x0C | 4 | offset of the object's name in the string table |
| 0x10 | 4 | component count |
| 0x14 | .. | component records, back to back, up to the string table |

The string table holds NUL-terminated names. Every name, bank or wave
field of a record is an offset into it.

## Component records

A record is a type word followed by a body of 32-bit words whose length
depends on the type (below). The first body word is always the
component's name. Walking the records of each shipped file with these
sizes ends exactly at its string table, which is what proves the table.

Values in a record are one of:

- a *triple* `(src, pin, const)`: `src == -1` means the float `const`;
  otherwise the value is component `src`'s control output `pin` (`const`
  is then not read);
- a *pair* `(src, pin)`: a control input that must be connected;
- a component index (an event target, an audio source, an effect), `-1`
  meaning none;
- a plain float or a count.

Body sizes in words (name included); `p(k)` is word `k` counted from the
type word:

| types | words |
|---|---|
| 0x03 0x06 0x09 0x0C 0x0E 0x10 0xB8 | 1 |
| 0x00 0x02 0x07 0x08 0x0D 0x0F 0x11 0x12 0xB9 | 2 |
| 0x26 0x29 0x2D 0xAC 0xB1 | 3 |
| 0x01 0x04 0x61 0x64 0xAB 0xAF | 4 |
| 0x18 0x23 | 6 |
| 0x1D | 7 |
| 0xB0 | 8 |
| 0x1E 0x27 0x73 | 9 |
| 0x1A 0xAE | 11 |
| 0x6D 0x6E 0x6F | 13 |
| 0x76 0x77 | 14 |
| 0x2E | 15 |
| 0x7F 0x80 0x81 0x82 0x83 0x94 0x95 0x96 0xAD 0xBC | 16 |
| 0x30 | 17 |
| 0x16 | 18 |
| 0x70 | 19 |
| 0x85 0x86 0x87 0x8B 0x8D 0x8E 0x90 0x91 0x92 0x93 | 22 |
| 0x17 | 24 |
| 0xB2 0xB3 | 31 |
| 0x15 | 48 |
| 0x66 | 0 |
| 0x13 | 7 + 4 p(7) |
| 0x1F 0x20 0x24 0x25 | 2 + 3 p(2) |
| 0x31 | 7 + 3 p(7) |
| 0x34 0x35 | 18 + p(18) |
| 0x36 | 21 + p(4) + p(21) |
| 0x38 0x39 | 20 + p(20) |
| 0x3D 0x40 0x45 0xB7 | 6 + p(5) + 4 p(6) |
| 0x3E 0xB6 | 9 + p(8) + 4 p(9) |
| 0x3F | 15 + p(14) + 4 p(15) |
| 0x41 | 12 + p(11) + 4 p(12) |
| 0x42 | 24 + p(23) + 4 p(24) |
| 0x46 | 3 + p(2) + 4 p(3) |
| 0xA9 | 3 + p(3) |
| 0xAA | 5 + 12 p(2) |
| 0xB5 | 18 + p(17) + 4 p(18) |
| 0xBA | 30 + p(29) + 4 p(30) |
| 0xBB | 42 + p(41) + 4 p(42) |

Any other type is rejected.

## The component types the shipped files use

Word numbers count body words, the name being word 0.

### Interface

| type | component | words |
|---|---|---|
| 0x00 | ParameterIn | 1 default value (float) |
| 0x01 | ParameterOut | 1..3 value triple |
| 0x02 | ActionIn | 1 target component |
| 0x0C / 0xB8 | MonoAudioInput / SurroundLFEAudioInput | (name only) |
| 0x0D / 0xB9 | MonoAudioOutput / SurroundLFEAudioOutput | 1 source component |
| 0xAB | DebugPrint | 1 suffix string, 2..3 input pair |

The game finds parameters, actions and outputs by exact name. Audio
inputs and outputs of the same name connect objects together.

### Control

| type | component | words |
|---|---|---|
| 0x12 | Variable | 1 initial value (float) |
| 0x13 | PiecewiseLinearFunction | 1..2 x pair, 3..5 shape, 6 point count N, then N x {x float, y triple} |
| 0x18 | LowPass | 1..2 input pair, 3..5 cutoff |
| 0x1A | Timer | 1..3 start value, 4..6 rate, 7..9 alarm time, 10 alarm target |
| 0x1D | Random | 1..3 minimum, 4..6 maximum |
| 0x1F 0x20 0x24 0x25 | Sum, Product, Max, Min | 1 count n, then n triples |
| 0x26 | Gradient | 1..2 input pair |
| 0x29 | Sin | 1..2 input pair |
| 0x2E | Envelope | 1..2 input pair, 3..5 attack, 6..8 hold, 9..11 release, 12..14 end targets |
| 0x30 | GateSwitch | 1..2 input pair, 3..5 threshold, 6..8 hysteresis, 9..11 low output, 12..14 high output, 15 rising target, 16 falling target |
| 0x31 | Array | 1..2 index pair, 3..5 offset, 6 element count N, then N triples |
| 0xA9 | Script | 1 unused, 2 code length n, then n code words |
| 0xAA | Selector | 1 entry count N, 2..4 minimum total, then N x {value, initial weight, multiplier, increment} triples |

Script code is a word stream of opcodes with inline operands (component
indices, pins, floats, absolute jump targets); the interpreter is in
`src/audio/components_control.cpp`.

### Sounds

| type | component | words |
|---|---|---|
| 0x34 / 0x35 | Mono / StereoOneShotSound | 1 bank, 2 wave, 3 voice count, 4..6 pitch (octaves), 7..9 rate ratio, 10..12 gain (dB), 13..15 gain, 16 finished target, 17 effect count, then the effects |
| 0x38 / 0x39 | Mono / StereoLoopingSound | 1 bank, 2 wave, 3..5 pitch, 6..8 rate ratio, 9..11 gain (dB), 12..14 gain, 15..17 start position (fraction), 18 finished target, 19 effect count, then the effects |

Bank and wave names are matched without regard to case.

### Mixers

A mixer body is its header triples, the effect count E, the input count
N, E effect component indices, then N x {source component, gain triple}.

| type | mixer | header triples |
|---|---|---|
| 0x3D | MonoToMono | gain |
| 0x3E | MonoToStereo | gain, pan |
| 0x40 | StereoToMono | balance |
| 0x41 | StereoToStereo | gain, left position, right position |
| 0x42 | StereoToSurround | gain, then azimuth, angle and distance for each side |
| 0x45 | SurroundToSurround | gain |
| 0xB5 | MonoToSurroundLFE | gain, azimuth, an unused triple, distance, LFE |
| 0xB6 | SurroundToSurroundLFE | gain, LFE |
| 0xB7 | SurroundLFEToSurroundLFE | gain |
| 0xBA | StereoToQuad | an unused triple, then a 2 x 4 matrix |
| 0xBB | QuadToSurround | gain, then 4 x (azimuth, unused, distance) |

### Effects

Effects are components attached to a mixer; they process its output.

| type | effect | words |
|---|---|---|
| 0x7F | MonoLowPass | 5 triples: frequency, octaves, multiplier, dB, gain |
| 0x82 / 0x83 | Mono / StereoHighPass | 5 triples, as the low-pass |
| 0x85 / 0x86 | Mono / StereoResonantLowPass | 7 triples: frequency, octaves, multiplier, resonance, dB, gain, and one the engine stores but never uses |
| 0x92 | StereoPeakEQ | 7 triples: frequency, octaves, multiplier, bandwidth, dB, gain, clip level |
| 0x77 | StereoFeedbackDelay | 1 maximum delay (seconds, float), then 4 triples: delay, feedback, wet gain, dry gain |
| 0xB2 | MonoDistortion | 10 triples: input dB and gain, shelf frequency and slope, second-shelf dB and gain, drive dB, knee, output level and gain |

# BANK wave banks (`audio/*.baf`)

The sound effects and the music are wave banks: `GW3.baf` (game sounds),
`MenuBank.baf` and `Song1Bank.baf` .. `Song6Bank.baf`. All values are
big-endian. The game's reader is `sub_820FEB10` (header and chunks) with
`sub_820FE8C0` (wave records).

## Header (0x34 bytes)

| offset | size | meaning |
|---|---|---|
| 0x00 | 4 | `BANK` |
| 0x04 | 4 | header size, 0x34 |
| 0x08 | 4 | version, 4 (others are rejected) |
| 0x0C | 4 | wave count (non-zero) |
| 0x10 | 1 | bank kind (checked against what the caller expects) |
| 0x11 | .. | bank name, NUL-terminated |

## Chunks

Then chunks to the end of the file, each a 4-byte tag and a 4-byte size
that includes the 8-byte chunk header: first one `WAVE` per wave, then
one `DATA` per wave holding its samples. Other tags are skipped.

### WAVE

| offset (after the chunk header) | size | meaning |
|---|---|---|
| 0x00 | 4 | format: 3 = 16-bit PCM (big-endian), 8 = XMA1 (the game also accepts 0, 7 and 9) |
| 0x04 | 32 | name |
| 0x24 | 4 | file offset of the samples (inside a DATA chunk) |
| 0x28 | 4 | sample data size |
| 0x2C | 4 | float, 1.0 in every shipped wave |
| 0x30 | 4 | flags (0x19 in every shipped wave) |
| 0x34 | .. | format data: sample rate, bits per sample, channels, then (XMA) four more words |

Formats 0 and 3 have 0x14 bytes of format data, 7 has 0x10, 8 and 9 the
rest of the chunk (0x20). XMA data is a whole number of 2048-byte XMA1
packets for one stream (mono or stereo, 48 kHz in the shipped banks).

## Decoding

`geow2-extract` writes each wave as 16-bit PCM: PCM waves byte-swapped,
XMA waves wrapped in a RIFF file with an `XMAWAVEFORMAT` header (format
tag 0x0165, one stream) and decoded by FFmpeg's `xma1` decoder.

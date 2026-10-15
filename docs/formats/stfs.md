# STFS package (as used by the XBLA release)

The release is one STFS package with magic `LIVE`. All integers are big
endian unless marked LE. Implemented in `src/extract/stfs.cpp`.

| offset | size | field |
|---|---|---|
| 0x000 | 4 | magic `LIVE` (`PIRS` and `CON ` share the layout) |
| 0x340 | 4 | header size; file data starts at the next 0x1000 boundary |
| 0x344 | 4 | content type (0x000D0000 = Arcade title) |
| 0x358 | 4 | version |
| 0x360 | 4 | title ID |
| 0x379 | 0x24 | volume descriptor |
| 0x411 | 0x80 | display name, UTF-16BE |

Volume descriptor fields used:

| offset | size | field |
|---|---|---|
| 0x37B | 1 | flags; bit 0 set = read-only layout (one hash table per level) |
| 0x37C | 2 LE | file table length in blocks |
| 0x37E | 3 LE | file table first block |

## Blocks

Data blocks are 0x1000 bytes. Each run of 170 data blocks is preceded by a
hash table block, and a further table precedes every 170^2 and 170^3
blocks. Block `b` is at

    data_start + 0x1000 * (b + sum over levels of (b + T) / T)

where `T` is 170, 170^2, 170^3 for the levels (doubled in the
non-read-only layout), stopping after the first level `b` is below.

## File table

Entries are 0x40 bytes; an entry with a zero name-length byte is unused.

| offset | size | field |
|---|---|---|
| 0x00 | 0x28 | name, ASCII, not terminated |
| 0x28 | 1 | bits 0-5 name length, bit 6 contiguous, bit 7 directory |
| 0x29 | 3 LE | valid blocks |
| 0x2C | 3 LE | allocated blocks |
| 0x2F | 3 LE | first block |
| 0x32 | 2 | parent directory entry index, -1 = root |
| 0x34 | 4 | size in bytes |
| 0x38 | 4 | update time stamp |
| 0x3C | 4 | access time stamp |

Every file of the release is contiguous, so the extractor reads
consecutive blocks and does not follow the hash tables' next-block links.

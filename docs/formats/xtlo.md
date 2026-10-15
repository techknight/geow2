# Localized text (`Text-<lang>.loc`, magic `XTLO`)

One file per language: `Text-en.loc`, `-de`, `-fr`, `-es`, `-it`. Big
endian. Implemented in `src/data/xtlo.cpp`.

## Header (16 bytes)

| offset | size | field |
|---|---|---|
| 0x00 | 4 | magic `XTLO` |
| 0x04 | 4 | version, 1 |
| 0x08 | 4 | file offset of the ID block |
| 0x0C | 4 | size of the ID block |

## ID block

Offsets below are relative to the start of the ID block.

| offset | size | field |
|---|---|---|
| 0x00 | 4 | offset of the language list |
| 0x04 | 4 | language count |
| 0x08 | 4 | offset of the ID table |

Language list entries are 0x18 bytes: a NUL-padded language name (16
bytes, e.g. `english`), then the file offset and size of that language's
string block.

The ID table: count (4), offset of the first entry from the table start
(4, = 8 in the shipped files), then 8-byte entries of (hash, offset of the
ID name). The name offset is relative to the ID table. IDs are ASCII,
NUL-terminated, and the table is sorted by name.

## String block

Count (4), offset of the first entry from the block start (4, = 8), then
8-byte entries of (hash, offset of the string) sorted by hash. String offsets are relative to the start of the
string block. Strings are UTF-16BE, NUL-terminated.

## Lookup

The game turns an ID name into a hash with a binary search of the ID
table, then the hash into a string with a binary search of the string
block. The hash values are only keys; nothing computes them.

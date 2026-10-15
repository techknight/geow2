# Flask database (`Data.bin`, magic `FLSK`)

`Data.bin` is a serialized database of the game's content objects: vector
models, units (ships and enemies), the mode icons and intermission models,
and progression levels. It carries its own schema, with type and field
names, which the game matches by name against the schema compiled into
the executable. Big endian. Implemented in `src/data/flask.cpp`.

## Header

`FLSK`, version (1 or 2; the shipped file is 2), then eight (offset,
count) pairs:

| words | section | entry size |
|---|---|---|
| 2, 3 | types | 12 |
| 4, 5 | base links | 4 |
| 6, 7 | fields | 8 |
| 8, 9 | name pool | 1 (bytes) |
| 10, 11 | records | 8 |
| 12, 13 | objects | 16 |
| 14, 15 | chunks | 6 |
| 16, 17 | data blob | 1 (bytes) |

## Obfuscation (version 2)

The name pool and each object's string bytes are obfuscated. With
`key = 0xCC`, for byte `i` of `n` (all arithmetic mod 256):
`out = key - (in ^ 0xB3)`, then `key = key - i + out`. The last byte
becomes a terminator. Version 1 stores them plain.

## Schema

Type (12 bytes): name offset (2), field count (2), base count (2), kind (2:
0 atom, 1 enum, 2 flags, 3 struct), atom code (2), size (2). Each type's
fields and bases are the next runs of the field and base sections.

Base link (4): type index (2), offset (2).

Field (8): name offset (2), type index (2; 0xFFFF for an enum value),
offset or value (2), field kind (1: 0 atom, 1 enum, 2 flags, 3 inline
struct, 4 pointer, 5 record reference, 6 enum value), array flag (1).

Atom codes: 1 bool, 2/6 signed/unsigned 8-bit, 3/7 16-bit, 4/8 32-bit, 5/9
64-bit, 10 f32, 11 f64, 12 string, 13 32-bit ID. Atoms narrower than 32
bits are stored in 4 bytes.

## Records and objects

Record (8): 32-bit ID (strictly increasing), type index (2), object index
(2; 0xFFFF for none).

Object (16): record index, chunk count, data size, string size (4 each).
Objects' data and string bytes follow one another through the blob.

Chunk (6): type index, element count, kind (2 each). Kind 0 elements are
the type's size, kinds 1 and 2 are 4 bytes. Each chunk starts at a
multiple of 4 within its object; the object's root is the first element
of its first chunk.

## Values

- Enum: a 4-byte index into the type's values.
- Flags: a 4-byte mask; bit k selects value k.
- String: a 4-byte offset into the object's (de-obfuscated) strings.
- Pointer (4 bytes): chunk index (2), element index (2) within the same
  object; the chunk must hold inline elements.
- Record reference (4 bytes): record index.
- Array (8 bytes): chunk index (2), first element (2), count (4). The
  chunk holds inline elements (kind 0), pointers (kind 1) or record
  references (kind 2), matching the field kind.

# XEX2 executable (`default.xex`)

Big endian throughout. Implemented in `src/extract/xex.cpp`; every rule
below is checked by the hashes the file carries, so a wrong step fails
loudly.

## Header

| offset | size | field |
|---|---|---|
| 0x00 | 4 | magic `XEX2` |
| 0x04 | 4 | module flags |
| 0x08 | 4 | offset of the image data |
| 0x10 | 4 | offset of the security info |
| 0x14 | 4 | optional header count |
| 0x18 | 8 each | optional headers: key, value |

An optional header whose key's low byte is 0x00 or 0x01 holds its value
inline; otherwise the value is a file offset. Keys used:

| key | meaning |
|---|---|
| 0x000003FF | file format info (encryption, compression) |
| 0x00010100 | entry point (inline) |
| 0x000103FF | import libraries |
| 0x000183FF | original PE name (size, then text) |

## Security info

| offset | size | field |
|---|---|---|
| 0x004 | 4 | image size |
| 0x10C | 4 | image flags; 0x10000000 = 4 KB pages, else 64 KB |
| 0x110 | 4 | load address |
| 0x114 | 20 | digest of the first page run (see below) |
| 0x150 | 16 | file key, AES-128 encrypted with the retail key |
| 0x180 | 4 | page descriptor count |
| 0x184 | 24 each | page descriptors: value (bits 4-31 page count, bits 0-3 type), 20-byte digest |

## Decoding

1. **Key.** AES-128-ECB decrypt the file key with the public retail key;
   the result is the session key.
2. **Decrypt.** AES-128-CBC decrypt everything from the image data offset
   to the end of the file with the session key and a zero IV.
3. **Decompress.** File format info: size (4), encryption (2), compression
   (2). For compression 2 ("normal"): window size (4), first block size
   (4), first block SHA-1 (20). Each block's SHA-1 must equal the hash
   given for it. A block starts with the next block's size and SHA-1 (24
   bytes), then frames, each a 2-byte length followed by that many bytes,
   until a zero length. The concatenated frames are one LZX stream (window
   from the header, no resets) that decodes to the image size.
   Compression 1 ("basic") is a list of (stored size, zero-fill size)
   pairs after the first 8 bytes; compression 0 is the image as is.
4. **Verify pages.** The descriptors cover the image in order. The
   security info's digest at 0x114 is the SHA-1 of the first run's bytes
   followed by the first descriptor (all 24 bytes). Each descriptor's
   digest is the SHA-1 of the next run followed by the next descriptor.

## Imports

The import header: size (4), string table size (4), library count (4),
the string table (names, each padded to 4 bytes), then per library: size
(4), digest (20), ID (4), version (4), minimum version (4), name index
(2), record count (2), record addresses (4 each). Each record address
points into the image at a dword whose top byte is 0 for the data record
or 1 for the call thunk, and whose low 16 bits are the ordinal.

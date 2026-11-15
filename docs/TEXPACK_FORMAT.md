# Texture data on the Xbox 360 disc (USA)

What `texpack/` reads, as found on the USA disc's installed files (`NFS/`). Layouts only: no game
data is reproduced here.

## Archive index: `NFS/ZDIR.BIN`

1390 entries of 24 bytes, little-endian, sorted by name hash:

| Offset | Field |
|---|---|
| 0x00 | name hash (EA's `h = h * 33 + c` from 0xFFFFFFFF over the upper-case path; `runtime/game/language.h` `ArchiveHash`) |
| 0x04 | archive: the file is in `ZZDATA<n>.BIN` |
| 0x08 | sector: the file starts at `sector * 0x800` |
| 0x0C | another sector count (not the size; unused here) |
| 0x10 | size in bytes (0 in 87 entries: nothing to read) |
| 0x14 | checksum (unused here) |

Checked by contiguity: for 1288 of 1380 neighbouring files, the next file starts at the first sector
after `sector * 0x800 + size`. `ZZDATA7.BIN` and the `PAD*.BIN` files are filler.

## Chunks

Every file is a list of chunks: an 8-byte little-endian header `{id, size}`, then `size` bytes. An
id with bit 31 set holds more chunks. A chunk that runs past its parent's end ends the list.

## TPK (texture pack), `0xB3300000`

| Chunk | Contents |
|---|---|
| `0xB3310000` | the records |
| `  0x33310001` | header: the pack's short name at 0x04, its path (e.g. `Global\…​.tpk`) at 0x20 |
| `  0x33310002` | the textures' name hashes (8 bytes each) |
| `  0x33310004` | one 124-byte record per texture (below) |
| `  0x33310005` | one 32-byte record per texture: at 0x14, the texture fetch constant's dword 1 as a template (format in bits 0–5, endianness in bits 6–7; its address is filled in at load) |
| `0xB3320000` | the pixels |
| `  0x33320003` | the data. Texture data starts at the next **4 KB boundary from the start of the file** (or of the stream section) after this chunk's payload starts, because the game loads files 4 KB-aligned |

The 124-byte texture record:

| Offset | Field |
|---|---|
| 0x0C | name, 24 bytes, NUL-padded |
| 0x24 | name hash |
| 0x28 | class hash |
| 0x30 | data offset, from the 4 KB-aligned data start |
| 0x38 | data size: base level and mips |
| 0x40 | base level size: exactly `texlayout::BaseLevel(format, width, height, width in blocks, tiled).size` |
| 0x44 | width (16 bits) |
| 0x46 | height (16 bits) |
| 0x48 | log2 width, log2 height (a byte each), then flags |

All textures seen are tiled, with a pitch of their own width (rounded up to 32 blocks).

## The key: blocks only

A texture in memory and the same texture on the disc have identical **blocks**, but not identical
padding: the tiled layout rounds each side up to 32 blocks, and the game reuses that padding memory
for other data (seen: 4 bytes of another structure inside a 128x64 DXT3 texture's padding rows). So
the texture pack's key hashes only the base level's own `bw x bh` blocks, gathered in row order
(`texlayout::GatherBase`).

## The city stream: compressed blocks

The archive's largest file (459 MB, name hash `0x4B91A993`) is a sequence of blocks:

| Offset | Field |
|---|---|
| 0x00 | `0x55441122` |
| 0x04 | uncompressed size (up to 0x8000) |
| 0x08 | block size in the file, this 24-byte header included |
| 0x0C | offset in its section (0 starts a new section) |
| 0x10, 0x14 | unused here |
| 0x18 | the compressed data: `JDLZ` (18103 blocks) or `HUFF` (9395 blocks) |

1029 sections; each is a chunk list like a file (TPKs at its top level). `texpack/jdlz.h` decodes
JDLZ. HUFF (EA's Huffman coding, header `HUFF` 01 10 00 00, uncompressed size, compressed size) is
not decoded yet: textures whose records or pixels fall in HUFF blocks are counted, not keyed.

## Coverage measured (USA disc, first race, 2026-11-15)

`texpack-extract --keys-only`: 281 TPKs, 2616 textures, 2243 keyed (1887 unique), 373 in HUFF
blocks. Against a 3-minute scripted run's 943 eligible textures (`NFSMW_TEXPACK_DUMP`): 600 matched
(63.6%). Of the 342 others, 223 are only in HUFF blocks, 98 are in decoded stream data but not yet
matched, 21 are in plain files in containers not parsed yet.

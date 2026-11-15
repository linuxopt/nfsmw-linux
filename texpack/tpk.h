// SpeedBreaker texture pack tools. GPL-3.0-or-later (see COPYING).
//
// The Xbox 360 TPK (texture pack) as the disc stores it; the layout, as
// found on the USA disc, is in docs/TEXPACK_FORMAT.md. Chunks are 8-byte
// little-endian {id, size} headers; an id with bit 31 set holds more
// chunks. A TPK (0xB3300000) holds its texture records (0xB3310000:
// 0x33310001 header with the pack's name, 0x33310004 one 124-byte record a
// texture, 0x33310005 one 32-byte record a texture with its fetch-constant
// template) and its pixel data (0xB3320000 > 0x33320003), which starts at
// the next 4 KB boundary from the start of the *file* the TPK is in (or of
// the stream section), as the game loads files 4 KB-aligned.
#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace texpack
{
    struct TpkSpan
    {
        size_t start, end;  // the TPK chunk's header and its end, in the file
    };

    struct TexRecord
    {
        std::string name, tpkName;
        uint32_t nameHash = 0, format = 0, endian = 0, width = 0, height = 0, mipCount = 1;
        bool tiled = true;
        size_t dataOffset = 0;  // the base level's first byte, in the file
        uint32_t baseSize = 0;  // the base level's bytes (texlayout::BaseLevel's size)
        uint32_t dataSize = 0;  // the base level and its mips
    };

    // Every TPK in `file`, at any depth of its chunk tree (up to 4 levels).
    // Stops at the first chunk that runs past its parent's end.
    void FindTpks(std::span<const uint8_t> file, std::vector<TpkSpan>& out);

    // Every TPK in `file` found by its signature: a 0xB3300000 header, at a
    // 4-byte boundary, whose chunk fits the file and holds texture records
    // (0xB3310000) and pixels (0xB3320000). For decoded stream sections
    // with holes, where a lost chunk header stops FindTpks' walk.
    void ScanTpks(std::span<const uint8_t> file, std::vector<TpkSpan>& out);

    bool ParseTpk(std::span<const uint8_t> file, const TpkSpan& tpk, std::vector<TexRecord>& out, std::string& error);

    // The record's base level lies inside a file of `fileSize` bytes.
    inline bool InFile(const TexRecord& r, size_t fileSize)
    {
        return r.baseSize != 0 && r.dataOffset + r.baseSize <= fileSize;
    }
}

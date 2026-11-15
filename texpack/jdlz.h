// SpeedBreaker texture pack tools. GPL-3.0-or-later (see COPYING).
//
// EA's JDLZ decompression, and the compressed block container the city
// stream (the archive's largest file) is stored in.
//
// JDLZ: "JDLZ" 02 10 00 00, the uncompressed size and the compressed size
// (little-endian), then the data: two flag bytes (refilled, each, when its
// 8 bits are used up), the first choosing literal or match per step, the
// second choosing a match's form: a short one (distance 1-16, length 3-4098)
// or a long one (distance 17-2064, length 3-34).
//
// Blocks: each is 0x55441122, its uncompressed size, its size in the file
// (24-byte header included), its offset in its section, and two words this
// doesn't use; then its compressed data (JDLZ, or HUFF, which this doesn't
// decode). A block at offset 0 starts the next section.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace texpack
{
    bool JdlzDecompress(const uint8_t* in, size_t size, std::vector<uint8_t>& out);

    struct Section
    {
        std::vector<uint8_t> data;
        std::vector<bool> known;  // per byte: decoded (false: a block this can't decode)
    };

    struct BlockStats
    {
        uint32_t jdlz = 0, other = 0, failed = 0;
    };

    void DecodeBlocks(const uint8_t* file, size_t size, std::vector<Section>& sections, BlockStats& stats);
}

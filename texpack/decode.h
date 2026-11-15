// SpeedBreaker texture pack tools. GPL-3.0-or-later (see COPYING).
//
// A texture's gathered blocks (texlayout::GatherBase: its own blocks, row by
// row, in guest byte order; from the disc or from play-capture) to RGBA8,
// exactly as the renderer's untile shader reads them: every 4-byte word
// little-endian, swapped by the Xenos endianness (1 8-in-16, 2 8-in-32,
// 3 16-in-32), gives the host image's bytes. DXT1/2-3/4-5 (formats 18, 19,
// 20) decode as BC1/2/3; 8888 (6, 14) is the bytes; 565 (4), 1555 (3) and
// 4444 (15) unpack as the shader converts them (red from bit 0). The
// replacement is made from this RGBA and sampled through the same swizzle,
// so its channels line up with the original's.
#pragma once
#include <cstdint>
#include <span>
#include <vector>

namespace texpack
{
    bool DecodeTexture(uint32_t format, uint32_t endian, uint32_t width, uint32_t height, std::span<const uint8_t> blocks,
        std::vector<uint8_t>& rgba);
}

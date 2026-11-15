// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// A Xenos texture's layout in guest memory (Xenia's texture_util): its
// format's block size, the tiled address of a texel, and the bytes of its
// base level. The renderer untiles from exactly this range, and the texture
// pack's extractor (texpack/) hashes exactly this range, so a texture read
// from the disc and the same texture in memory get the same key. Vulkan-free
// (the formats' VkFormat values are plain ints; renderer.cpp checks them
// against Vulkan's), for tests/texture_layout_test.cpp and texpack/.
#pragma once
#include <cstdint>

namespace gpu::texlayout
{
    // VkFormat values the formats map to (vulkan_core.h's enum).
    constexpr int kVkR8 = 9, kVkRg8 = 16, kVkRgba8 = 37, kVkA2b10g10r10 = 64, kVkR16 = 70, kVkR16f = 76, kVkRg16 = 77,
                  kVkRg16f = 83, kVkRgba16 = 91, kVkRgba16f = 97, kVkR32f = 100, kVkRg32f = 103, kVkRgba32f = 109,
                  kVkBc1 = 133, kVkBc2 = 135, kVkBc3 = 137, kVkBc5 = 141;

    struct FormatInfo
    {
        uint32_t bpbLog2;   // guest bytes per block
        uint32_t block;     // block width/height in texels
        uint32_t convert;   // 0 copy; 1 565, 2 1555, 3 4444 -> RGBA8; 4 24_8 -> R32F
        int vkFormat;       // the host format (a VkFormat)
    };

    constexpr bool GetFormat(uint32_t f, FormatInfo& out)
    {
        switch (f)
        {
        case 2: out = { 0, 1, 0, kVkR8 }; return true;
        case 3: out = { 1, 1, 2, kVkRgba8 }; return true;
        case 4: out = { 1, 1, 1, kVkRgba8 }; return true;
        case 6: case 14: out = { 2, 1, 0, kVkRgba8 }; return true;
        case 7: out = { 2, 1, 0, kVkA2b10g10r10 }; return true;
        case 10: out = { 1, 1, 0, kVkRg8 }; return true;
        case 15: out = { 1, 1, 3, kVkRgba8 }; return true;
        case 18: out = { 3, 4, 0, kVkBc1 }; return true;
        case 19: out = { 4, 4, 0, kVkBc2 }; return true;
        case 20: out = { 4, 4, 0, kVkBc3 }; return true;
        case 22: case 23: out = { 2, 1, 4, kVkR32f }; return true;
        case 24: out = { 1, 1, 0, kVkR16 }; return true;
        case 25: out = { 2, 1, 0, kVkRg16 }; return true;
        case 26: out = { 3, 1, 0, kVkRgba16 }; return true;
        case 30: out = { 1, 1, 0, kVkR16f }; return true;
        case 31: out = { 2, 1, 0, kVkRg16f }; return true;
        case 32: out = { 3, 1, 0, kVkRgba16f }; return true;
        case 36: out = { 2, 1, 0, kVkR32f }; return true;
        case 37: out = { 3, 1, 0, kVkRg32f }; return true;
        case 38: out = { 4, 1, 0, kVkRgba32f }; return true;
        case 49: out = { 4, 4, 0, kVkBc5 }; return true;
        default: return false;
        }
    }

    // The byte offset of block (x, y) in a tiled 2D surface `pitchAligned`
    // blocks wide, 1 << bppLog2 bytes a block.
    constexpr uint32_t Tiled2D(uint32_t x, uint32_t y, uint32_t pitchAligned, uint32_t bppLog2)
    {
        uint32_t outer = (((y >> 5) * (pitchAligned >> 5)) + (x >> 5)) << 6;
        uint32_t inner = (((y >> 1) & 7) << 3) | (x & 7);
        uint32_t oi = (outer | inner) << bppLog2;
        uint32_t bank = (y >> 4) & 1;
        uint32_t pipe = ((x >> 3) & 3) ^ (((y >> 3) & 1) << 1);
        return ((y & 1) << 4) | (pipe << 6) | (bank << 11) | (oi & 0xF) | (((oi >> 4) & 1) << 5) |
            (((oi >> 5) & 7) << 8) | ((oi >> 8) << 12);
    }

    // A 2D, single-layer base level: tiled, its pitch and height in blocks
    // rounded up to 32 (pitchAligned, heightAligned); linear, rows of
    // rowBytes (256-aligned). offX/offY: where a packed-mip base level sits
    // in its tile, in blocks. size: the bytes from the base address.
    struct Base
    {
        uint32_t pitchAligned, rowBytes, heightAligned, size;
    };

    constexpr Base BaseLevel(const FormatInfo& fi, uint32_t width, uint32_t height, uint32_t pitchBlocks, bool tiled,
        uint32_t offX, uint32_t offY)
    {
        uint32_t bw = (width + fi.block - 1) / fi.block, bh = (height + fi.block - 1) / fi.block;
        Base b{};
        if (tiled)
        {
            b.pitchAligned = ((pitchBlocks > bw + offX ? pitchBlocks : bw + offX) + 31) & ~31u;
            b.heightAligned = (bh + offY + 31) & ~31u;
            b.size = (b.pitchAligned * b.heightAligned) << fi.bpbLog2;
        }
        else
        {
            b.rowBytes = ((pitchBlocks << fi.bpbLog2) + 255) & ~255u;
            b.heightAligned = (bh + offY + 31) & ~31u;
            b.size = b.rowBytes * (bh + offY);
        }
        return b;
    }
}

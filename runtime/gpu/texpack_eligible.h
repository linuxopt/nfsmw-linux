// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// Which textures the texture pack may replace: plain 2D art the game loaded
// from the disc. Never what the GPU draws (scaled textures, a resolve's
// target, shadow maps), cubes, volumes, stacked or multi-layer textures, a
// base level sitting at a packed-mip offset, or formats the pack never
// holds (BC5 normal maps, single/dual-channel and float data). Tiny ones
// (both sides 16 px or less, or 1 px thin) are lookup data, as
// texpack/classify.h decides on the disc side. Vulkan-free, for
// tests/texpack_eligible_test.cpp.
#pragma once
#include <cstdint>

namespace gpu::texpack
{
    struct EligibleIn
    {
        bool cube = false, volume = false, stacked = false, scaled = false, shadowMap = false, resolveTarget = false;
        uint32_t layers = 1, offX = 0, offY = 0, format = 0, width = 0, height = 0;
    };

    // The Xenos formats a replacement can stand in for: DXT1/2-3/4-5, 8888,
    // and the 16-bit colour formats (565, 1555, 4444).
    constexpr bool PackFormat(uint32_t format)
    {
        switch (format)
        {
        case 3: case 4: case 6: case 14: case 15: case 18: case 19: case 20: return true;
        default: return false;
        }
    }

    constexpr bool Eligible(const EligibleIn& in)
    {
        if (in.cube || in.volume || in.stacked || in.scaled || in.shadowMap || in.resolveTarget)
            return false;
        if (in.layers != 1 || in.offX != 0 || in.offY != 0)
            return false;
        if (!PackFormat(in.format))
            return false;
        if (in.width <= 1 || in.height <= 1 || (in.width <= 16 && in.height <= 16))
            return false;
        return true;
    }
}

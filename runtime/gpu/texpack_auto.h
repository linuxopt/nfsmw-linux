// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// Settings > Graphics > HD Textures (Off / Auto / 1K / 2K) and its Auto, as
// the renderer applies them to the texture pack (gpu/texpack.h). One pack is
// built at 2K; a machine loads each replacement from the mip level that fits
// its cap. Auto, by the GPU:
//   discrete, 8 GB or more:   2K
//   discrete, 4 to 8 GB:      1K
//   discrete, under 4 GB:     1K, and only for originals 128 px or larger
//   integrated (Steam Deck):  1K while Internal Resolution is 1.5x or less,
//                             otherwise off (shared memory and bandwidth go
//                             to the resolution; 2K detail can't show on
//                             their screens anyway)
// No BC7 support: off whatever the setting. On top of the cap, a budget: the
// replacements may use half the device memory the game itself leaves free
// (TexpackBudgetMb); past it, the least recently used fall back to their
// originals. Vulkan-free, for tests/texpack_auto_test.cpp.
#pragma once
#include <cstdint>

namespace gpu::texpack
{
    constexpr int32_t kHdTexturesAuto = 0, kHdTexturesOff = 1, kHdTextures1K = 2, kHdTextures2K = 3;

    struct TexpackDevice
    {
        bool discrete, steamDeck, bc7;
        uint64_t deviceLocalMb;
        double internalScale;  // Internal Resolution in use (1, 1.5, 2, 3)
    };

    struct TexpackChoice
    {
        bool on = false;
        uint32_t capPx = 0;          // a replacement's long side at most
        uint32_t minOriginalPx = 0;  // replace only originals whose long side is at least this
    };

    constexpr TexpackChoice AutoTexpack(const TexpackDevice& d, int32_t setting)
    {
        if (!d.bc7 || setting == kHdTexturesOff)
            return {};
        if (setting == kHdTextures1K)
            return { true, 1024, 0 };
        if (setting == kHdTextures2K)
            return { true, 2048, 0 };
        if (!d.discrete || d.steamDeck)
        {
            if (d.internalScale > 1.5)
                return {};
            return { true, 1024, 0 };
        }
        if (d.deviceLocalMb >= 8192)
            return { true, 2048, 0 };
        if (d.deviceLocalMb >= 4096)
            return { true, 1024, 0 };
        return { true, 1024, 128 };
    }

    constexpr uint64_t TexpackBudgetMb(uint64_t heapMb, uint64_t gameUsedMb)
    {
        return gameUsedMb >= heapMb ? 0 : (heapMb - gameUsedMb) / 2;
    }
}

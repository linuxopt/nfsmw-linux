// SpeedBreaker tests. GPL-3.0-or-later (see COPYING).
//
// HD Textures' Auto (gpu/texpack_auto.h): the replacement size each kind of
// machine loads, the settings that override it, and the memory budget.
//
// Build by hand: c++ -std=c++20 -O2 -UNDEBUG -I runtime tests/texpack_auto_test.cpp -o build/texpack_auto_test
#include <gpu/texpack_auto.h>

#include <cstdio>

static int s_failures = 0;
#define CHECK(cond, what)                                              \
    do                                                                 \
    {                                                                  \
        if (!(cond))                                                   \
        {                                                              \
            s_failures++;                                              \
            fprintf(stderr, "FAIL %s:%d: %s: %s\n", __FILE__, __LINE__, #cond, what); \
        }                                                              \
    } while (0)

using namespace gpu::texpack;

static TexpackDevice Dev(bool discrete, uint64_t mb, double scale = 1.0, bool deck = false, bool bc7 = true)
{
    return TexpackDevice{ discrete, deck, bc7, mb, scale };
}

int main()
{
    TexpackChoice c;
    c = AutoTexpack(Dev(true, 16384), kHdTexturesAuto);
    CHECK(c.on && c.capPx == 2048 && c.minOriginalPx == 0, "16 GB discrete: 2K");
    c = AutoTexpack(Dev(true, 8192), kHdTexturesAuto);
    CHECK(c.on && c.capPx == 2048, "8 GB discrete: 2K");
    c = AutoTexpack(Dev(true, 6144), kHdTexturesAuto);
    CHECK(c.on && c.capPx == 1024 && c.minOriginalPx == 0, "6 GB discrete: 1K");
    c = AutoTexpack(Dev(true, 2048), kHdTexturesAuto);
    CHECK(c.on && c.capPx == 1024 && c.minOriginalPx == 128, "2 GB discrete: 1K, larger originals only");
    c = AutoTexpack(Dev(false, 16384, 1.0, true), kHdTexturesAuto);
    CHECK(c.on && c.capPx == 1024, "Steam Deck at 1x: 1K");
    c = AutoTexpack(Dev(false, 16384, 2.0, true), kHdTexturesAuto);
    CHECK(!c.on, "Steam Deck at 2x: off");
    c = AutoTexpack(Dev(false, 8192, 1.5), kHdTexturesAuto);
    CHECK(c.on && c.capPx == 1024, "integrated at 1.5x: 1K");
    c = AutoTexpack(Dev(false, 8192, 2.0), kHdTexturesAuto);
    CHECK(!c.on, "integrated at 2x: off");
    c = AutoTexpack(Dev(true, 16384, 1.0, false, false), kHdTexturesAuto);
    CHECK(!c.on, "no BC7: off");

    c = AutoTexpack(Dev(true, 16384), kHdTexturesOff);
    CHECK(!c.on, "Off");
    c = AutoTexpack(Dev(false, 8192, 3.0), kHdTextures2K);
    CHECK(c.on && c.capPx == 2048, "2K overrides Auto");
    c = AutoTexpack(Dev(true, 2048), kHdTextures1K);
    CHECK(c.on && c.capPx == 1024 && c.minOriginalPx == 0, "1K overrides Auto");
    c = AutoTexpack(Dev(true, 16384, 1.0, false, false), kHdTextures2K);
    CHECK(!c.on, "no BC7 overrides even 2K");

    CHECK(TexpackBudgetMb(16000, 2000) == 7000, "half of what the game leaves");
    CHECK(TexpackBudgetMb(2000, 3000) == 0, "nothing left: no budget");

    if (s_failures)
        fprintf(stderr, "%d failure(s)\n", s_failures);
    else
        printf("texpack_auto_test: all passed\n");
    return s_failures ? 1 : 0;
}

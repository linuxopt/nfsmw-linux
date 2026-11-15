// SpeedBreaker tests. GPL-3.0-or-later (see COPYING).
//
// Which textures the texture pack may replace (gpu/texpack_eligible.h): 2D,
// one layer, no packed-mip offset, not drawn by the GPU (scaled, a resolve's
// target, a shadow map), in a format the pack holds, larger than 16 px.
// Argument: runtime/gpu/renderer.cpp (it must decide with this rule).
//
// Build by hand: c++ -std=c++20 -O2 -UNDEBUG -I runtime tests/texpack_eligible_test.cpp -o build/texpack_eligible_test
#include <gpu/texpack_eligible.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

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

using gpu::texpack::Eligible;
using gpu::texpack::EligibleIn;

static EligibleIn Plain()
{
    EligibleIn in{};
    in.layers = 1;
    in.format = 18;  // DXT1
    in.width = 256;
    in.height = 256;
    return in;
}

int main(int argc, char** argv)
{
    CHECK(Eligible(Plain()), "a plain DXT1 256x256");
    for (uint32_t f : { 18u, 19u, 20u, 6u, 14u, 4u, 3u, 15u })
    {
        EligibleIn in = Plain();
        in.format = f;
        CHECK(Eligible(in), "a format the pack holds");
    }
    EligibleIn in;
    in = Plain(); in.cube = true; CHECK(!Eligible(in), "cube");
    in = Plain(); in.volume = true; CHECK(!Eligible(in), "volume");
    in = Plain(); in.stacked = true; CHECK(!Eligible(in), "stacked");
    in = Plain(); in.scaled = true; CHECK(!Eligible(in), "scaled");
    in = Plain(); in.shadowMap = true; CHECK(!Eligible(in), "shadow map");
    in = Plain(); in.resolveTarget = true; CHECK(!Eligible(in), "a resolve's target");
    in = Plain(); in.layers = 2; CHECK(!Eligible(in), "two layers");
    in = Plain(); in.offX = 4; CHECK(!Eligible(in), "packed-mip offset x");
    in = Plain(); in.offY = 4; CHECK(!Eligible(in), "packed-mip offset y");
    in = Plain(); in.format = 49; CHECK(!Eligible(in), "BC5 (normal maps)");
    in = Plain(); in.format = 36; CHECK(!Eligible(in), "float data");
    in = Plain(); in.width = 1; CHECK(!Eligible(in), "1 px wide");
    in = Plain(); in.width = 16; in.height = 16; CHECK(!Eligible(in), "16x16 and smaller");
    in = Plain(); in.width = 32; in.height = 16; CHECK(Eligible(in), "32x16");

    if (argc > 1)
    {
        std::ifstream f(argv[1]);
        std::stringstream ss;
        ss << f.rdbuf();
        CHECK(ss.str().find("texpack::Eligible(") != std::string::npos, "the renderer uses the rule");
    }

    if (s_failures)
        fprintf(stderr, "%d failure(s)\n", s_failures);
    else
        printf("texpack_eligible_test: all passed\n");
    return s_failures ? 1 : 0;
}

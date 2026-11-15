// SpeedBreaker tests. GPL-3.0-or-later (see COPYING).
//
// A texture's base level in guest memory (gpu/texture_layout.h): the bytes
// the renderer untiles, and the texture pack's extractor hashes. Both must
// compute the same range, so the renderer must use the header, not its own
// copy. Argument: runtime/gpu/renderer.cpp.
//
// Build by hand: c++ -std=c++20 -O2 -UNDEBUG -I runtime tests/texture_layout_test.cpp -o build/texture_layout_test
#include <gpu/texture_layout.h>

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

using namespace gpu::texlayout;

int main(int argc, char** argv)
{
    FormatInfo dxt1{}, dxt5{}, rgba8{}, bc5{};
    CHECK(GetFormat(18, dxt1) && dxt1.block == 4 && dxt1.bpbLog2 == 3, "DXT1: 8-byte 4x4 blocks");
    CHECK(GetFormat(20, dxt5) && dxt5.block == 4 && dxt5.bpbLog2 == 4, "DXT5: 16-byte 4x4 blocks");
    CHECK(GetFormat(6, rgba8) && rgba8.block == 1 && rgba8.bpbLog2 == 2, "8888: 4 bytes a texel");
    CHECK(GetFormat(49, bc5) && bc5.vkFormat == kVkBc5, "DXN is BC5");
    FormatInfo none{};
    CHECK(!GetFormat(63, none), "unknown format refused");

    // DXT1 256x256, tiled, pitch 64 blocks: 64x64 blocks of 8 bytes.
    Base b = BaseLevel(dxt1, 256, 256, 64, true, 0, 0);
    CHECK(b.pitchAligned == 64 && b.size == 32768, "DXT1 256x256 tiled");
    // DXT5 512x256, tiled, pitch 128 blocks.
    b = BaseLevel(dxt5, 512, 256, 128, true, 0, 0);
    CHECK(b.size == 131072, "DXT5 512x256 tiled");
    // 8888 100x50, linear, pitch 128 texels: rows of 512 bytes (256-aligned).
    b = BaseLevel(rgba8, 100, 50, 128, false, 0, 0);
    CHECK(b.rowBytes == 512 && b.size == 512 * 50, "8888 100x50 linear");
    // Tiled heights and pitches round up to 32 blocks.
    b = BaseLevel(dxt1, 40, 8, 10, true, 0, 0);
    CHECK(b.pitchAligned == 32 && b.heightAligned == 32 && b.size == (32 * 32) << 3, "small tiled rounds to 32x32 blocks");
    // A packed-mip base level offset into its tile widens the range.
    b = BaseLevel(dxt1, 16, 16, 4, true, 4, 0);
    CHECK(b.pitchAligned == 32 && b.size == (32 * 32) << 3, "offX inside the tile");

    // Tiled2D: texel (0,0) is at 0; neighbours differ.
    CHECK(Tiled2D(0, 0, 32, 2) == 0, "origin");
    CHECK(Tiled2D(1, 0, 32, 2) != Tiled2D(0, 1, 32, 2), "x and y map apart");

    if (argc > 1)
    {
        std::ifstream f(argv[1]);
        std::stringstream ss;
        ss << f.rdbuf();
        std::string src = ss.str();
        CHECK(!src.empty(), "renderer.cpp read");
        CHECK(src.find("texlayout::BaseLevel(") != std::string::npos, "the renderer sizes base levels with the header");
        CHECK(src.find("uint32_t Tiled2D(uint32_t x") == std::string::npos, "the renderer has no Tiled2D of its own");
    }

    if (s_failures)
        fprintf(stderr, "%d failure(s)\n", s_failures);
    else
        printf("texture_layout_test: all passed\n");
    return s_failures ? 1 : 0;
}

// SpeedBreaker tests. GPL-3.0-or-later (see COPYING).
//
// The game's side of the texture pack (gpu/texpack.h): it indexes a pack
// folder by file name, loads a replacement starting at the first mip level
// that fits the resolution cap, refuses a truncated file (an interrupted
// copy between machines) and a pack built for another disc region.
// Synthetic DDS files in a temporary folder.
//
// Build by hand: c++ -std=c++20 -O2 -UNDEBUG -I runtime tests/texpack_pack_test.cpp runtime/gpu/texpack.cpp -o build/texpack_pack_test
#include <gpu/texpack.h>

#include <cstdio>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

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

namespace fs = std::filesystem;

static void Put32(std::vector<uint8_t>& v, size_t at, uint32_t x)
{
    for (int i = 0; i < 4; i++)
        v[at + i] = uint8_t(x >> (8 * i));
}

// A BC7 DDS of w x h with a full chain; each level's blocks filled with its level number.
static std::vector<uint8_t> Dds(uint32_t w, uint32_t h)
{
    uint32_t levels = 1;
    for (uint32_t s = std::max(w, h); s > 1; s >>= 1)
        levels++;
    std::vector<uint8_t> d(148, 0);
    memcpy(d.data(), "DDS ", 4);
    Put32(d, 4, 124);
    Put32(d, 12, h);
    Put32(d, 16, w);
    Put32(d, 28, levels);
    Put32(d, 76, 32);
    Put32(d, 80, 4);
    memcpy(&d[84], "DX10", 4);
    Put32(d, 128, 98);
    Put32(d, 132, 3);
    Put32(d, 140, 1);
    for (uint32_t l = 0; l < levels; l++)
    {
        const uint32_t lw = std::max(1u, w >> l), lh = std::max(1u, h >> l);
        d.insert(d.end(), size_t((lw + 3) / 4) * ((lh + 3) / 4) * 16, uint8_t(l));
    }
    return d;
}

static void Write(const fs::path& p, const std::vector<uint8_t>& v)
{
    std::ofstream(p, std::ios::binary).write(reinterpret_cast<const char*>(v.data()), std::streamsize(v.size()));
}

int main()
{
    const fs::path dir = fs::temp_directory_path() / ("texpack_pack_test_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    using gpu::texpack::Pack;
    std::string why;

    {
        Pack p;
        CHECK(!p.Open(dir, "usa", why) && !why.empty(), "no pack.json: no pack");
    }
    std::ofstream(dir / "pack.json") << "{\"version\": 1, \"region\": \"usa\", \"model\": \"m\", \"count\": 3}\n";
    Write(dir / "00000000000000aa.dds", Dds(2048, 1024));
    Write(dir / "00000000000000bb.dds", Dds(64, 64));
    std::vector<uint8_t> torn = Dds(256, 256);
    torn.resize(torn.size() - 100);
    Write(dir / "00000000000000cc.dds", torn);
    Write(dir / "00000000000000dd.dds.partial", Dds(64, 64));
    Write(dir / "notakey.dds", Dds(64, 64));

    Pack p;
    CHECK(p.Open(dir, "usa", why), why.c_str());
    CHECK(p.Count() == 3, "three replacements (no .partial, no odd names)");
    CHECK(p.Has(0xaa) && p.Has(0xbb) && !p.Has(0xdd) && !p.Has(0x1234), "Has");

    Pack::Image img;
    CHECK(p.Load(0xaa, 4096, img, why) && img.width == 2048 && img.height == 1024 && img.levels == 12, "full size under the cap");
    CHECK(p.Load(0xaa, 1024, img, why) && img.width == 1024 && img.height == 512 && img.levels == 11, "capped: from level 1");
    CHECK(img.levelOffsets.size() == 11 && img.levelOffsets[0] == 0 && img.data[0] == 1, "starts at level 1's blocks");
    CHECK(img.levelOffsets.size() == 11 && img.data[img.levelOffsets[1]] == 2, "then level 2");
    CHECK(img.data.size() == img.levelOffsets.back() + 16, "ends with the 1x1 level's block");
    CHECK(p.Load(0xbb, 1024, img, why) && img.width == 64, "small ones unchanged");
    CHECK(!p.Load(0xcc, 4096, img, why) && !why.empty(), "a truncated file is refused");
    CHECK(!p.Load(0x1234, 4096, img, why), "a key not in the pack");

    Pack other;
    CHECK(!other.Open(dir, "pal_es", why) && why.find("usa") != std::string::npos, "another region's pack is ignored");

    fs::remove_all(dir);
    if (s_failures)
        fprintf(stderr, "%d failure(s)\n", s_failures);
    else
        printf("texpack_pack_test: all passed\n");
    return s_failures ? 1 : 0;
}

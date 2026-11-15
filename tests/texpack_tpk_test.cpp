// SpeedBreaker tests. GPL-3.0-or-later (see COPYING).
//
// The Xbox 360 TPK (texture pack) layout as texpack/tpk.h reads it
// (docs/TEXPACK_FORMAT.md): a synthetic pack of two textures, nested in a
// container chunk, its data 4 KB-aligned from the start of the file; and a
// torn one. No game data.
//
// Build by hand: c++ -std=c++20 -O2 -UNDEBUG -I . -I runtime tests/texpack_tpk_test.cpp texpack/tpk.cpp -o build/texpack_tpk_test
#include <texpack/tpk.h>

#include <cstdio>
#include <cstring>
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

static void Put32At(std::vector<uint8_t>& v, size_t at, uint32_t x)
{
    for (int i = 0; i < 4; i++)
        v[at + i] = uint8_t(x >> (8 * i));
}

// [container 0x80034100 [tpk 0xB3300000 [info 0xB3310000 [1, 4, 5]] [data 0xB3320000 [3]]]]
static std::vector<uint8_t> BuildFile()
{
    std::vector<uint8_t> f(0x3000, 0);
    size_t at = 0;
    auto chunk = [&](size_t pos, uint32_t id, uint32_t size) { Put32At(f, pos, id); Put32At(f, pos + 4, size); };
    const size_t container = 0x10, tpk = container + 8;
    const size_t info = tpk + 8, recs1 = info + 8, recs4 = recs1 + 8 + 0x7c, recs5 = recs4 + 8 + 2 * 124;
    const size_t infoEnd = recs5 + 8 + 2 * 32;
    const size_t data = infoEnd, payload = data + 8 + 8;
    const size_t end = 0x3000;
    chunk(container, 0x80034100, uint32_t(end - container - 8));
    chunk(tpk, 0xB3300000, uint32_t(end - tpk - 8));
    chunk(info, 0xB3310000, uint32_t(infoEnd - info - 8));
    chunk(recs1, 0x33310001, 0x7c);
    memcpy(&f[recs1 + 8 + 0x20], "Synthetic\\Pack.tpk", 18);
    chunk(recs4, 0x33310004, 2 * 124);
    chunk(recs5, 0x33310005, 2 * 32);
    chunk(data, 0xB3320000, uint32_t(end - data - 8));
    chunk(data + 8, 0x33320003, uint32_t(end - payload));
    // Texture 0: DXT1 64x64 at data offset 0 (base 0x2000: 32x32 aligned blocks of 8).
    size_t r = recs4 + 8;
    memcpy(&f[r + 0xC], "ROAD_A", 6);
    Put32At(f, r + 0x24, 0x11111111);
    Put32At(f, r + 0x30, 0);
    Put32At(f, r + 0x38, 0x2000);
    Put32At(f, r + 0x40, 0x2000);
    f[r + 0x44] = 64; f[r + 0x46] = 64;
    Put32At(f, r + 0x38, 0x2000 + 0x800);  // with mips: a full chain
    // Texture 1: 8888 32x32 at data offset 0x2000... past the file: torn, skipped.
    r += 124;
    memcpy(&f[r + 0xC], "SIGN_B", 6);
    Put32At(f, r + 0x24, 0x22222222);
    Put32At(f, r + 0x30, 0x2000);
    Put32At(f, r + 0x38, 0x1000);
    Put32At(f, r + 0x40, 0x1000);
    f[r + 0x44] = 32; f[r + 0x46] = 32;
    // Fetch dword 1 templates: DXT1 (18) 8-in-16; 8888 (6) 8-in-32.
    Put32At(f, recs5 + 8 + 0x14, 0x1a200000 | 18 | (1 << 6));
    Put32At(f, recs5 + 8 + 32 + 0x14, 0x18280000 | 6 | (2 << 6));
    (void)at;
    return f;
}

int main()
{
    std::vector<uint8_t> f = BuildFile();
    std::vector<texpack::TpkSpan> tpks;
    texpack::FindTpks(f, tpks);
    CHECK(tpks.size() == 1 && tpks[0].start == 0x18, "the TPK inside its container");

    std::vector<texpack::TexRecord> recs;
    std::string error;
    CHECK(tpks.size() == 1 && texpack::ParseTpk(f, tpks[0], recs, error), error.c_str());
    CHECK(recs.size() == 2, "two records");
    if (recs.size() == 2)
    {
        const texpack::TexRecord& a = recs[0];
        CHECK(a.name == "ROAD_A" && a.nameHash == 0x11111111, "name and hash");
        CHECK(a.format == 18 && a.endian == 1 && a.width == 64 && a.height == 64 && a.mipCount == 7, "format and shape");
        CHECK(a.dataOffset == 0x1000 && a.baseSize == 0x2000, "data 4 KB-aligned from the file's start");
        CHECK(a.tpkName == "Synthetic\\Pack.tpk", "the pack's name");
        CHECK(recs[1].format == 6 && recs[1].endian == 2 && recs[1].dataOffset == 0x3000 && recs[1].mipCount == 1, "second record, no mips");
    }
    CHECK(!texpack::InFile(recs.size() == 2 ? recs[1] : texpack::TexRecord{}, f.size()), "a texture past the file's end");
    CHECK(recs.size() == 2 && texpack::InFile(recs[0], f.size()), "a texture inside it");

    // Torn: the TPK's chunk claims more than the file holds.
    std::vector<uint8_t> torn(f.begin(), f.begin() + 0x200);
    tpks.clear();
    texpack::FindTpks(torn, tpks);
    CHECK(tpks.empty(), "a torn chunk is no TPK");

    // Garbage: no crash, nothing found.
    std::vector<uint8_t> junk(4096, 0xFF);
    tpks.clear();
    texpack::FindTpks(junk, tpks);
    CHECK(tpks.empty(), "junk");

    // ScanTpks: a TPK behind a chunk the walk can't get past (its header lost
    // in an undecoded block, here 0xFF bytes) is found by its signature; a
    // stray 0xB3300000 inside pixel data isn't a TPK.
    {
        std::vector<uint8_t> lost(0x40, 0xFF);
        std::vector<uint8_t> g = BuildFile();
        const size_t shift = lost.size() + 0x1000 - (lost.size() % 0x1000);  // keep the 4 KB data alignment: pad to 0x1000
        std::vector<uint8_t> h(shift, 0xFF);
        h.insert(h.end(), g.begin(), g.end());
        // A stray signature with a size that fits but holds no records.
        h[0x80] = 0x00; h[0x81] = 0x00; h[0x82] = 0x30; h[0x83] = 0xB3;
        h[0x84] = 0x10; h[0x85] = 0x00; h[0x86] = 0x00; h[0x87] = 0x00;
        std::vector<texpack::TpkSpan> walked, scanned;
        texpack::FindTpks(h, walked);
        CHECK(walked.empty(), "the walk stops at the lost header");
        texpack::ScanTpks(h, scanned);
        CHECK(scanned.size() == 1 && scanned[0].start == shift + 0x18, "the scan finds the real TPK only");
        std::vector<texpack::TexRecord> r2;
        CHECK(scanned.size() == 1 && texpack::ParseTpk(h, scanned[0], r2, error) && r2.size() == 2 && r2[0].dataOffset == shift + 0x1000,
            "and parses it in place");
    }

    if (s_failures)
        fprintf(stderr, "%d failure(s)\n", s_failures);
    else
        printf("texpack_tpk_test: all passed\n");
    return s_failures ? 1 : 0;
}

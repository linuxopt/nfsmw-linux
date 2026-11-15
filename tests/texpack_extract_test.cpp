// SpeedBreaker tests. GPL-3.0-or-later (see COPYING).
//
// Full extraction (texpack/extract.h): the disc's textures and the
// play-captured ones become one set, by key: a PNG of each colour or UI
// texture for the upscaler, and manifest.json listing every texture with
// its class, source and alpha. Synthetic archive and capture: no game data.
//
// Build by hand: see tests/CMakeLists.txt (links texpack_core).
#include <texpack/extract.h>
#include <gpu/texpack_key.h>
#include <gpu/texture_layout.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
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
    if (v.size() < at + 4)
        v.resize(at + 4);
    for (int i = 0; i < 4; i++)
        v[at + i] = uint8_t(x >> (8 * i));
}

static std::string Slurp(const fs::path& p)
{
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// A TPK file with one DXT1 64x64 texture (opaque red, 8-in-16) and one BC5 32x32.
static std::vector<uint8_t> TpkFile()
{
    std::vector<uint8_t> f(0x1000 + 0x2000 + 0x1000, 0);
    const size_t tpk = 0, info = 8, r1 = info + 8, r4 = r1 + 8 + 0x7c, r5 = r4 + 8 + 2 * 124, infoEnd = r5 + 8 + 2 * 32;
    const size_t data = infoEnd, end = f.size();
    Put32(f, tpk, 0xB3300000); Put32(f, tpk + 4, uint32_t(end - 8));
    Put32(f, info, 0xB3310000); Put32(f, info + 4, uint32_t(infoEnd - info - 8));
    Put32(f, r1, 0x33310001); Put32(f, r1 + 4, 0x7c);
    memcpy(&f[r1 + 8 + 0x20], "Tracks\\Synthetic.tpk", 20);
    Put32(f, r4, 0x33310004); Put32(f, r4 + 4, 2 * 124);
    Put32(f, r5, 0x33310005); Put32(f, r5 + 4, 2 * 32);
    Put32(f, data, 0xB3320000); Put32(f, data + 4, uint32_t(end - data - 8));
    Put32(f, data + 8, 0x33320003); Put32(f, data + 12, uint32_t(end - data - 16));
    size_t r = r4 + 8;
    memcpy(&f[r + 0xC], "ROAD_A", 6);
    Put32(f, r + 0x30, 0); Put32(f, r + 0x38, 0x2000); Put32(f, r + 0x40, 0x2000);
    f[r + 0x44] = 64; f[r + 0x46] = 64;
    r += 124;
    memcpy(&f[r + 0xC], "ROAD_A_N", 8);
    Put32(f, r + 0x30, 0x2000); Put32(f, r + 0x38, 0x1000); Put32(f, r + 0x40, 0x1000);
    f[r + 0x44] = 32; f[r + 0x46] = 32;
    Put32(f, r5 + 8 + 0x14, 18 | (1 << 6));
    Put32(f, r5 + 8 + 32 + 0x14, 49 | (1 << 6));
    // Red DXT1 blocks (8-in-16 on the disc) at the 4 KB-aligned data start, tiled.
    gpu::texlayout::FormatInfo fi{};
    gpu::texlayout::GetFormat(18, fi);
    const auto base = gpu::texlayout::BaseLevel(fi, 64, 64, 16, true, 0, 0);
    for (uint32_t y = 0; y < 16; y++)
        for (uint32_t x = 0; x < 16; x++)
        {
            uint8_t* b = &f[0x1000 + gpu::texlayout::Tiled2D(x, y, base.pitchAligned, 3)];
            b[0] = 0xF8; b[1] = 0x00; b[2] = 0x00; b[3] = 0x1F;
        }
    return f;
}

int main()
{
    const fs::path root = fs::temp_directory_path() / ("texpack_extract_test_" + std::to_string(::getpid()));
    fs::remove_all(root);
    const fs::path nfs = root / "NFS", capture = root / "capture", work = root / "work";
    fs::create_directories(nfs);
    fs::create_directories(capture);

    // The same TPK file twice in the archive (as the disc repeats packs).
    std::vector<uint8_t> tpk = TpkFile(), zz;
    zz.insert(zz.end(), tpk.begin(), tpk.end());
    zz.insert(zz.end(), tpk.begin(), tpk.end());
    std::ofstream(nfs / "ZZDATA0.BIN", std::ios::binary).write(reinterpret_cast<const char*>(zz.data()), std::streamsize(zz.size()));
    std::vector<uint8_t> zdir;
    for (uint32_t i = 0; i < 2; i++)
    {
        const size_t at = zdir.size();
        Put32(zdir, at, 0x100 + i);
        Put32(zdir, at + 4, 0);
        Put32(zdir, at + 8, uint32_t(i * tpk.size() / 0x800));
        Put32(zdir, at + 12, 0);
        Put32(zdir, at + 16, uint32_t(tpk.size()));
        Put32(zdir, at + 20, 0);
    }
    std::ofstream(nfs / "ZDIR.BIN", std::ios::binary).write(reinterpret_cast<const char*>(zdir.data()), std::streamsize(zdir.size()));

    // One captured texture: 8888 32x32, half transparent (1-bit alpha), 8-in-32.
    std::vector<uint8_t> cap(32 * 32 * 4);
    for (size_t i = 0; i < 32 * 32; i++)
    {
        const uint8_t a = i < 512 ? 0xFF : 0x00;
        cap[i * 4 + 0] = a; cap[i * 4 + 1] = 0x30; cap[i * 4 + 2] = 0x20; cap[i * 4 + 3] = 0x10;  // reversed: r 0x10 g 0x20 b 0x30
    }
    const uint64_t capKey = gpu::texpack::Key(cap.data(), cap.size(), 6, 32, 32);
    const std::string capName = gpu::texpack::KeyName(capKey);
    std::ofstream(capture / (capName + ".blk"), std::ios::binary).write(reinterpret_cast<const char*>(cap.data()), std::streamsize(cap.size()));
    std::ofstream(capture / (capName + ".json")) << "{\"format\": 6, \"endian\": 2, \"width\": 32, \"height\": 32}\n";

    texpack::ExtractOptions opt;
    opt.nfsDir = nfs;
    opt.captureDir = capture;
    opt.workDir = work;
    opt.region = "usa";
    opt.jobs = 4;
    std::string error;
    texpack::ExtractStats st;
    CHECK(texpack::Extract(opt, st, error), error.c_str());

    CHECK(st.unique == 3, "three textures: the repeated DXT1 once, the BC5, the captured one");
    CHECK(st.fromDisc == 2 && st.fromCapture == 1, "by source");
    CHECK(st.pngs == 2, "PNGs for the colour textures only");
    size_t pngs = 0;
    for (auto& e : fs::directory_iterator(work / "src"))
        pngs += e.path().extension() == ".png";
    CHECK(pngs == 2, "two PNG files");
    CHECK(fs::exists(work / "src" / (capName + ".png")), "the captured texture's PNG");
    const std::string m = Slurp(work / "manifest.json");
    CHECK(m.find("\"region\": \"usa\"") != std::string::npos, "region");
    CHECK(m.find("\"class\": \"normal\"") != std::string::npos, "the BC5 listed as a normal map");
    CHECK(m.find("\"source\": \"capture\"") != std::string::npos && m.find("\"alpha\": \"1bit\"") != std::string::npos,
        "the captured texture: source and 1-bit alpha");
    CHECK(m.find("\"alpha\": \"none\"") != std::string::npos, "the opaque DXT1");
    CHECK(m.find("\"tpk\": \"Tracks\\\\Synthetic.tpk\"") != std::string::npos, "its pack (JSON-escaped)");

    // A second run keeps existing PNGs (resumable).
    texpack::ExtractStats again;
    CHECK(texpack::Extract(opt, again, error) && again.pngs == 0 && again.unique == 3, "existing PNGs aren't redone");

    fs::remove_all(root);
    if (s_failures)
        fprintf(stderr, "%d failure(s)\n", s_failures);
    else
        printf("texpack_extract_test: all passed\n");
    return s_failures ? 1 : 0;
}

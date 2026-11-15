// SpeedBreaker tests. GPL-3.0-or-later (see COPYING).
//
// Play-capture (gpu/texpack_capture.h): textures the disc extraction can't
// reach are saved once each, as <key>.blk (their gathered blocks) and
// <key>.json (format, endianness, size), written whole (never a torn file),
// remembered across runs, and stopped at a size cap. Temporary folder.
//
// Build by hand: c++ -std=c++20 -O2 -UNDEBUG -I runtime tests/texpack_capture_test.cpp runtime/gpu/texpack_capture.cpp -o build/texpack_capture_test
#include <gpu/texpack_capture.h>

#include <cstdio>
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
using gpu::texpack::Capture;
using gpu::texpack::CaptureInfo;

static std::string Slurp(const fs::path& p)
{
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int main(int argc, char** argv)
{
    const fs::path dir = fs::temp_directory_path() / ("texpack_capture_test_" + std::to_string(::getpid()));
    fs::remove_all(dir);

    std::vector<uint8_t> blocks(4096);
    for (size_t i = 0; i < blocks.size(); i++)
        blocks[i] = uint8_t(i);
    {
        Capture c(dir, 10000);
        CHECK(c.Want(0x1234, 4096), "a new key is wanted");
        c.Submit(0x1234, CaptureInfo{ 18, 1, 64, 128 }, blocks);
        CHECK(!c.Want(0x1234, 4096), "once only");
        c.Flush();
        CHECK(Slurp(dir / "0000000000001234.blk") == std::string(blocks.begin(), blocks.end()), "the blocks, whole");
        const std::string json = Slurp(dir / "0000000000001234.json");
        CHECK(json.find("\"format\": 18") != std::string::npos && json.find("\"endian\": 1") != std::string::npos &&
                json.find("\"width\": 64") != std::string::npos && json.find("\"height\": 128") != std::string::npos,
            "its format, endianness and size");
        // Over the cap (10000 bytes): 4096 + 4096 fits, the third doesn't.
        c.Submit(0x2, CaptureInfo{ 18, 1, 64, 128 }, blocks);
        CHECK(!c.Want(0x3, 4096), "the cap stops capture");
        c.Flush();
        CHECK(c.Count() == 2 && c.Bytes() == 8192, "counted");
    }
    size_t partials = 0;
    for (auto& e : fs::directory_iterator(dir))
        partials += e.path().extension() == ".partial";
    CHECK(partials == 0, "no partial files left");
    {
        Capture again(dir, 1 << 20);
        CHECK(!again.Want(0x1234, 4096) && !again.Want(0x2, 4096), "remembered across runs");
        CHECK(again.Want(0x5, 4096), "others still wanted");
        CHECK(again.Count() == 2, "counts what's there");
    }
    {
        Capture none(fs::path(), 1 << 20);
        CHECK(!none.Want(0x9, 4096), "no folder: nothing wanted");
    }

    fs::remove_all(dir);

    // The renderer captures with it, when NFSMW_TEXPACK_CAPTURE is set.
    if (argc > 1)
    {
        const std::string src = Slurp(argv[1]);
        CHECK(src.find("NFSMW_TEXPACK_CAPTURE") != std::string::npos, "the switch");
        CHECK(src.find("s_texpackCapture->Submit(") != std::string::npos, "the renderer submits textures");
    }
    if (s_failures)
        fprintf(stderr, "%d failure(s)\n", s_failures);
    else
        printf("texpack_capture_test: all passed\n");
    return s_failures ? 1 : 0;
}

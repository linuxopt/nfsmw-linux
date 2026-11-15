// SpeedBreaker tests. GPL-3.0-or-later (see COPYING).
//
// The pack's encoder (texpack/encode.h): an upscaled PNG becomes a BC7 DDS
// (DX10 header, full mip chain), its long side capped at 2048; a result whose
// colour drifts from the original is refused; tiling textures are padded
// with their own wrapped edges before upscaling and cropped after; the
// Lanczos resize keeps flat colour flat. Temporary files, synthetic images.
//
// Build by hand: see tests/CMakeLists.txt.
#include <texpack/encode.h>

#include <cstdio>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <texpack/thirdparty/stb/stb_image_write.h>

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

static std::vector<uint8_t> Pattern(uint32_t w, uint32_t h, int add = 0, bool halfAlpha = false)
{
    std::vector<uint8_t> v(size_t(w) * h * 4);
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++)
        {
            uint8_t* p = &v[(size_t(y) * w + x) * 4];
            p[0] = uint8_t(std::min(255, int((x * 255) / w) + add));
            p[1] = uint8_t(std::min(255, int((y * 255) / h) + add));
            p[2] = uint8_t(std::min(255, 128 + add));
            p[3] = halfAlpha && x >= w / 2 ? 0 : 255;
        }
    return v;
}

// A 4x upscale stand-in: nearest neighbour.
static std::vector<uint8_t> Nearest4(const std::vector<uint8_t>& src, uint32_t w, uint32_t h)
{
    std::vector<uint8_t> v(size_t(w) * h * 64);
    for (uint32_t y = 0; y < h * 4; y++)
        for (uint32_t x = 0; x < w * 4; x++)
            memcpy(&v[(size_t(y) * w * 4 + x) * 4], &src[(size_t(y / 4) * w + x / 4) * 4], 4);
    return v;
}

static void Png(const fs::path& p, const std::vector<uint8_t>& rgba, uint32_t w, uint32_t h)
{
    stbi_write_png(p.c_str(), int(w), int(h), 4, rgba.data(), int(w * 4));
}

static uint32_t Le32(const std::string& s, size_t at)
{
    return uint8_t(s[at]) | uint8_t(s[at + 1]) << 8 | uint8_t(s[at + 2]) << 16 | uint32_t(uint8_t(s[at + 3])) << 24;
}

static std::string Slurp(const fs::path& p)
{
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
}

int main()
{
    const fs::path dir = fs::temp_directory_path() / ("texpack_encode_test_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    std::string info;

    // 64x64 -> 256x256 BC7 with 9 levels.
    {
        auto src = Pattern(64, 64);
        Png(dir / "a.png", src, 64, 64);
        Png(dir / "a_up.png", Nearest4(src, 64, 64), 256, 256);
        texpack::EncodeOptions o;
        o.src = dir / "a.png";
        o.up = dir / "a_up.png";
        o.out = dir / "a.dds";
        CHECK(texpack::EncodeTexture(o, info) == texpack::EncodeResult::Ok, info.c_str());
        const std::string d = Slurp(o.out);
        CHECK(d.size() > 148 && d.compare(0, 4, "DDS ") == 0, "DDS magic");
        CHECK(d.size() > 148 && Le32(d, 12) == 256 && Le32(d, 16) == 256, "256x256");
        CHECK(d.size() > 148 && Le32(d, 28) == 9, "9 mip levels");
        CHECK(d.size() > 148 && d.compare(84, 4, "DX10") == 0 && Le32(d, 128) == 98, "DX10 header, BC7_UNORM");
        size_t blocks = 0;
        for (uint32_t s = 256; s >= 1; s /= 2)
            blocks += size_t((s + 3) / 4) * ((s + 3) / 4);
        CHECK(d.size() == 148 + blocks * 16, "every level's blocks");
    }
    // 1024x512 with a 4096x2048 upscale: capped to 2048x1024.
    {
        auto src = Pattern(1024, 512);
        Png(dir / "b.png", src, 1024, 512);
        Png(dir / "b_up.png", Nearest4(src, 1024, 512), 4096, 2048);
        texpack::EncodeOptions o;
        o.src = dir / "b.png";
        o.up = dir / "b_up.png";
        o.out = dir / "b.dds";
        CHECK(texpack::EncodeTexture(o, info) == texpack::EncodeResult::Ok, info.c_str());
        const std::string d = Slurp(o.out);
        CHECK(d.size() > 148 && Le32(d, 12) == 1024 && Le32(d, 16) == 2048, "long side capped at 2048 (height 1024, width 2048)");
    }
    // Colour drift: refused, nothing written.
    {
        auto src = Pattern(32, 32);
        Png(dir / "c.png", src, 32, 32);
        Png(dir / "c_up.png", Nearest4(Pattern(32, 32, 40), 32, 32), 128, 128);
        texpack::EncodeOptions o;
        o.src = dir / "c.png";
        o.up = dir / "c_up.png";
        o.out = dir / "c.dds";
        CHECK(texpack::EncodeTexture(o, info) == texpack::EncodeResult::Drift, "drift refused");
        CHECK(!fs::exists(o.out), "no file for a refused texture");
    }
    // Wrap padding: 8x8 padded by 2 is 12x12, its corner the opposite corner's texel.
    {
        auto src = Pattern(8, 8);
        Png(dir / "d.png", src, 8, 8);
        CHECK(texpack::PadWrapped(dir / "d.png", dir / "d_pad.png", 2, info), info.c_str());
        // Upscaled padded image (48x48), encoded with pad 2: cropped to 32x32.
        std::vector<uint8_t> padded(12 * 12 * 4);
        for (uint32_t y = 0; y < 12; y++)
            for (uint32_t x = 0; x < 12; x++)
                memcpy(&padded[(y * 12 + x) * 4], &src[(((y + 6) % 8) * 8 + (x + 6) % 8) * 4], 4);
        Png(dir / "d_up.png", Nearest4(padded, 12, 12), 48, 48);
        texpack::EncodeOptions o;
        o.src = dir / "d.png";
        o.up = dir / "d_up.png";
        o.out = dir / "d.dds";
        o.pad = 2;
        CHECK(texpack::EncodeTexture(o, info) == texpack::EncodeResult::Ok, info.c_str());
        const std::string d = Slurp(o.out);
        CHECK(d.size() > 148 && Le32(d, 12) == 32 && Le32(d, 16) == 32, "the padding cropped off");
    }
    // 1-bit alpha: accepted.
    {
        auto src = Pattern(32, 32, 0, true);
        Png(dir / "e.png", src, 32, 32);
        Png(dir / "e_up.png", Nearest4(src, 32, 32), 128, 128);
        texpack::EncodeOptions o;
        o.src = dir / "e.png";
        o.up = dir / "e_up.png";
        o.out = dir / "e.dds";
        o.alpha = "1bit";
        CHECK(texpack::EncodeTexture(o, info) == texpack::EncodeResult::Ok, info.c_str());
    }
    // Lanczos: flat stays flat.
    {
        std::vector<uint8_t> flat(64 * 64 * 4, 77), out(40 * 24 * 4);
        texpack::ResizeLanczos(flat.data(), 64, 64, out.data(), 40, 24);
        bool same = true;
        for (uint8_t v : out)
            same = same && v == 77;
        CHECK(same, "flat colour stays flat");
    }
    // Missing input: an error, not a crash.
    {
        texpack::EncodeOptions o;
        o.src = dir / "missing.png";
        o.up = dir / "missing_up.png";
        o.out = dir / "x.dds";
        CHECK(texpack::EncodeTexture(o, info) == texpack::EncodeResult::Error, "missing input");
    }

    fs::remove_all(dir);
    if (s_failures)
        fprintf(stderr, "%d failure(s)\n", s_failures);
    else
        printf("texpack_encode_test: all passed\n");
    return s_failures ? 1 : 0;
}

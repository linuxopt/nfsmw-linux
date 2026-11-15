// SpeedBreaker tests. GPL-3.0-or-later (see COPYING).
//
// Decoding a texture's gathered blocks (guest byte order) to RGBA8 for the
// upscaler (texpack/decode.h), exactly as the renderer's untile shader
// reads them: each 4-byte word little-endian, swapped by its Xenos
// endianness; DXT1/3/5 as BC1/2/3; the 16-bit formats with the shader's
// channel order. And which textures go to the AI upscaler (classify.h).
//
// Build by hand: c++ -std=c++20 -O2 -UNDEBUG -I . -I runtime tests/texpack_decode_test.cpp texpack/decode.cpp -o build/texpack_decode_test
#include <texpack/classify.h>
#include <texpack/decode.h>

#include <cstdio>
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

static bool Texel(const std::vector<uint8_t>& rgba, uint32_t width, uint32_t x, uint32_t y, int r, int g, int b, int a)
{
    const uint8_t* p = &rgba[(size_t(y) * width + x) * 4];
    return p[0] == r && p[1] == g && p[2] == b && p[3] == a;
}

int main()
{
    std::vector<uint8_t> rgba;

    // BC1, 8-in-16 as DXT textures are stored: c0 = 0xF800 (red), c1 = 0x001F
    // (blue), every index 0 -> opaque red.
    {
        std::vector<uint8_t> b = { 0xF8, 0x00, 0x00, 0x1F, 0, 0, 0, 0 };
        CHECK(texpack::DecodeTexture(18, 1, 4, 4, b, rgba) && rgba.size() == 64, "BC1 decodes");
        CHECK(Texel(rgba, 4, 0, 0, 255, 0, 0, 255) && Texel(rgba, 4, 3, 3, 255, 0, 0, 255), "opaque red");
    }
    // BC1 with c0 <= c1: index 3 is transparent black.
    {
        std::vector<uint8_t> b = { 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
        CHECK(texpack::DecodeTexture(18, 0, 4, 4, b, rgba) && Texel(rgba, 4, 1, 2, 0, 0, 0, 0), "1-bit alpha");
    }
    // BC3: alpha endpoints 255/0, alpha index 1 everywhere -> alpha 0; colour white.
    {
        std::vector<uint8_t> b = { 0xFF, 0x00, 0x49, 0x92, 0x24, 0x49, 0x92, 0x24,  // alpha: a0 255, a1 0, indices 1
            0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0 };
        CHECK(texpack::DecodeTexture(20, 0, 4, 4, b, rgba) && Texel(rgba, 4, 2, 1, 255, 255, 255, 0), "BC3 alpha");
    }
    // Two 4x4 blocks side by side: an 8x4 texture, blocks in row order.
    {
        std::vector<uint8_t> b = { 0x00, 0xF8, 0x1F, 0x00, 0, 0, 0, 0,     // red (endian 0)
                                   0x1F, 0x00, 0x00, 0xF8, 0, 0, 0, 0 };   // blue
        CHECK(texpack::DecodeTexture(18, 0, 8, 4, b, rgba) && Texel(rgba, 8, 1, 1, 255, 0, 0, 255) && Texel(rgba, 8, 6, 2, 0, 0, 255, 255),
            "blocks in row order");
    }
    // 8888, 8-in-32: the word's bytes reversed.
    {
        std::vector<uint8_t> b = { 0x44, 0x33, 0x22, 0x11 };
        CHECK(texpack::DecodeTexture(6, 2, 1, 1, b, rgba) && Texel(rgba, 1, 0, 0, 0x11, 0x22, 0x33, 0x44), "8888 8-in-32");
    }
    // 565, as the untile shader converts it: red from bits 0-4, blue from 11-15.
    {
        std::vector<uint8_t> b = { 0x00, 0xF8, 0x1F, 0x00 };  // texel 0 = 0xF800, texel 1 = 0x001F
        CHECK(texpack::DecodeTexture(4, 0, 2, 1, b, rgba) && Texel(rgba, 2, 0, 0, 0, 0, 255, 255) && Texel(rgba, 2, 1, 0, 255, 0, 0, 255),
            "565");
    }
    // 4444: r bits 0-3, a bits 12-15.
    {
        std::vector<uint8_t> b = { 0x0F, 0xF0, 0x00, 0x00 };
        CHECK(texpack::DecodeTexture(15, 0, 2, 1, b, rgba) && Texel(rgba, 2, 0, 0, 255, 0, 0, 255), "4444");
    }
    // Too few bytes for the size: refused.
    {
        std::vector<uint8_t> b(8);
        CHECK(!texpack::DecodeTexture(18, 0, 8, 8, b, rgba), "short input");
        CHECK(!texpack::DecodeTexture(49, 0, 4, 4, std::vector<uint8_t>(16), rgba), "BC5 isn't decoded for the upscaler");
    }

    using texpack::Classify;
    using texpack::TexClass;
    CHECK(Classify(49, 512, 512, "ROAD_N", "") == TexClass::Normal, "BC5");
    CHECK(Classify(18, 512, 512, "CAR_BODY_NORMAL", "") == TexClass::Normal, "named a normal map");
    CHECK(Classify(18, 256, 256, "WALL_BUMP", "") == TexClass::Normal, "named a bump map");
    CHECK(Classify(18, 16, 16, "X", "") == TexClass::Data, "16x16");
    CHECK(Classify(18, 1, 256, "RAMP", "") == TexClass::Data, "1 px wide");
    CHECK(Classify(36, 256, 256, "LUT", "") == TexClass::Data, "float");
    CHECK(Classify(20, 512, 512, "CAR_SKIN", "CARS\\BMWM3GTR\\TEXTURES.BIN") == TexClass::Color, "car art");
    CHECK(Classify(20, 256, 64, "FONT_MW_BODY", "Global\\GlobalMessageTextures.tpk") == TexClass::Ui, "a font");
    CHECK(Classify(18, 256, 256, "", "FrontEnd\\FrontEndTextures.tpk") == TexClass::Ui, "front end");
    CHECK(Classify(18, 256, 256, "", "") == TexClass::Color, "a captured texture (no names)");

    if (s_failures)
        fprintf(stderr, "%d failure(s)\n", s_failures);
    else
        printf("texpack_decode_test: all passed\n");
    return s_failures ? 1 : 0;
}

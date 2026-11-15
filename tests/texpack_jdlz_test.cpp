// SpeedBreaker tests. GPL-3.0-or-later (see COPYING).
//
// EA's JDLZ decompression (texpack/jdlz.h), as the city stream's blocks use
// it: hand-built streams for its literals and both match forms, a stream
// that ends early, and the compressed block container
// (0x55441122 {uncompressed, compressed, offset in section}).
//
// Build by hand: c++ -std=c++20 -O2 -UNDEBUG -I . tests/texpack_jdlz_test.cpp texpack/jdlz.cpp -o build/texpack_jdlz_test
#include <texpack/jdlz.h>

#include <cstdio>
#include <cstring>
#include <string>
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

static void Put32(std::vector<uint8_t>& v, uint32_t x)
{
    for (int i = 0; i < 4; i++)
        v.push_back(uint8_t(x >> (8 * i)));
}

static std::vector<uint8_t> Jdlz(uint32_t usize, const std::vector<uint8_t>& body)
{
    std::vector<uint8_t> v = { 'J', 'D', 'L', 'Z', 0x02, 0x10, 0x00, 0x00 };
    Put32(v, usize);
    Put32(v, uint32_t(16 + body.size()));
    v.insert(v.end(), body.begin(), body.end());
    return v;
}

int main()
{
    std::vector<uint8_t> out;

    // "abc" then a short-distance match (flag2 bit set): distance 3, length 6.
    {
        auto in = Jdlz(9, { 0x08, 0x01, 'a', 'b', 'c', 0x02, 0x03 });
        CHECK(texpack::JdlzDecompress(in.data(), in.size(), out) && out.size() == 9 && memcmp(out.data(), "abcabcabc", 9) == 0,
            "literals and a short match");
    }
    // 20 literals, then a long-distance match (flag2 bit clear): distance 17, length 3.
    {
        std::vector<uint8_t> body = { 0x00, 0x00 };
        for (uint8_t i = 0; i < 8; i++)
            body.push_back(i);
        body.push_back(0x00);
        for (uint8_t i = 8; i < 16; i++)
            body.push_back(i);
        body.push_back(0x10);
        for (uint8_t i = 16; i < 20; i++)
            body.push_back(i);
        body.push_back(0x00);
        body.push_back(0x00);
        auto in = Jdlz(23, body);
        bool ok = texpack::JdlzDecompress(in.data(), in.size(), out);
        CHECK(ok && out.size() == 23 && out[20] == 3 && out[21] == 4 && out[22] == 5 && out[19] == 19, "a long match");
    }
    // Ends before its stated size: refused.
    {
        auto in = Jdlz(100, { 0x00, 0x00, 'x' });
        CHECK(!texpack::JdlzDecompress(in.data(), in.size(), out), "a stream shorter than its size");
    }
    // A match reaching before the start: refused, never reads out of bounds.
    {
        auto in = Jdlz(4, { 0x01, 0x01, 0x0F, 0x01 });
        CHECK(!texpack::JdlzDecompress(in.data(), in.size(), out), "a match before the start");
    }
    // Not JDLZ: refused.
    {
        std::vector<uint8_t> in(32, 0);
        CHECK(!texpack::JdlzDecompress(in.data(), in.size(), out), "no JDLZ tag");
    }

    // The block container: two JDLZ blocks of one section, then a new section.
    {
        std::vector<uint8_t> file;
        auto block = [&](uint32_t offset, const std::vector<uint8_t>& jdlz, uint32_t usize) {
            Put32(file, 0x55441122);
            Put32(file, usize);
            Put32(file, uint32_t(0x18 + jdlz.size()));
            Put32(file, offset);
            Put32(file, 0);
            Put32(file, 0);
            file.insert(file.end(), jdlz.begin(), jdlz.end());
        };
        block(0, Jdlz(9, { 0x08, 0x01, 'a', 'b', 'c', 0x02, 0x03 }), 9);
        block(9, Jdlz(3, { 0x00, 0x00, 'x', 'y', 'z' }), 3);
        block(0, Jdlz(3, { 0x00, 0x00, 'n', 'e', 'w' }), 3);
        std::vector<texpack::Section> sections;
        texpack::BlockStats stats;
        texpack::DecodeBlocks(file.data(), file.size(), sections, stats);
        CHECK(sections.size() == 2, "two sections");
        CHECK(sections.size() == 2 && sections[0].data.size() == 12 && memcmp(sections[0].data.data(), "abcabcabcxyz", 12) == 0,
            "a section's blocks in place");
        CHECK(sections.size() == 2 && sections[0].known.size() == 12 && sections[0].known[11], "decoded bytes are known");
        CHECK(sections.size() == 2 && memcmp(sections[1].data.data(), "new", 3) == 0, "the next section");
        CHECK(stats.jdlz == 3 && stats.other == 0, "counted");
    }
    // A block in another format: its bytes stay unknown, the section goes on.
    {
        std::vector<uint8_t> file;
        Put32(file, 0x55441122); Put32(file, 4); Put32(file, 0x18 + 8); Put32(file, 0); Put32(file, 0); Put32(file, 0);
        for (char c : std::string("HUFF....")) file.push_back(uint8_t(c));
        std::vector<texpack::Section> sections;
        texpack::BlockStats stats;
        texpack::DecodeBlocks(file.data(), file.size(), sections, stats);
        CHECK(sections.size() == 1 && sections[0].data.size() == 4 && !sections[0].known[0], "unknown bytes marked");
        CHECK(stats.other == 1, "counted as other");
    }

    if (s_failures)
        fprintf(stderr, "%d failure(s)\n", s_failures);
    else
        printf("texpack_jdlz_test: all passed\n");
    return s_failures ? 1 : 0;
}

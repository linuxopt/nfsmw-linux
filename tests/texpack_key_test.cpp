// SpeedBreaker tests. GPL-3.0-or-later (see COPYING).
//
// The texture pack's content key (gpu/texpack_key.h): the same base-level
// bytes and shape give the same key in the renderer and in the extractor;
// any byte or shape change gives another.
//
// Build by hand: c++ -std=c++20 -O2 -UNDEBUG -I runtime tests/texpack_key_test.cpp -o build/texpack_key_test
#include <gpu/texpack_key.h>

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

using namespace gpu::texpack;

int main()
{
    std::vector<uint8_t> a(32768);
    for (size_t i = 0; i < a.size(); i++)
        a[i] = uint8_t(i * 7 + 3);
    std::vector<uint8_t> b = a;

    CHECK(Key(a.data(), a.size(), 18, 256, 256) == Key(b.data(), b.size(), 18, 256, 256), "same bytes, same shape: same key");
    b[12345] ^= 1;
    CHECK(Key(a.data(), a.size(), 18, 256, 256) != Key(b.data(), b.size(), 18, 256, 256), "one flipped bit: another key");
    CHECK(Key(a.data(), a.size(), 18, 256, 256) != Key(a.data(), a.size(), 18, 128, 512), "same bytes, other shape: another key");
    CHECK(Key(a.data(), a.size(), 18, 256, 256) != Key(a.data(), a.size(), 20, 256, 256), "same bytes, other format: another key");

    CHECK(KeyName(0x1234) == "0000000000001234", "16 lowercase hex digits");
    CHECK(KeyName(0xABCDEF0123456789ull) == "abcdef0123456789", "lowercase");

    std::vector<uint8_t> zeros(4u << 20);
    CHECK(Key(zeros.data(), zeros.size(), 6, 1024, 1024) == Key(zeros.data(), zeros.size(), 6, 1024, 1024), "4 MB, stable");

    if (s_failures)
        fprintf(stderr, "%d failure(s)\n", s_failures);
    else
        printf("texpack_key_test: all passed\n");
    return s_failures ? 1 : 0;
}

// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// The texture pack's content key: XXH3-64 of a texture's base-level bytes
// exactly as they sit in guest memory (tiled, endian-swapped as on the disc;
// gpu/texture_layout.h's BaseLevel range), mixed with its Xenos format and
// size. The renderer computes it from guest memory, texpack/ from the disc's
// TPK data; a replacement is <KeyName>.dds. Mips aren't hashed: the base
// level identifies a texture, and a replacement brings its own levels.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

#define XXH_INLINE_ALL
#include <thirdparty/xxhash/xxhash.h>

namespace gpu::texpack
{
    inline uint64_t Key(const uint8_t* base, size_t size, uint32_t format, uint32_t width, uint32_t height)
    {
        uint64_t shape = 0xCBF29CE484222325ull;
        for (uint64_t v : { uint64_t(format), uint64_t(width), uint64_t(height) })
            shape = (shape ^ v) * 0x100000001B3ull;
        return XXH3_64bits(base, size) ^ shape;
    }

    inline std::string KeyName(uint64_t key)
    {
        static const char kHex[] = "0123456789abcdef";
        std::string s(16, '0');
        for (int i = 15; i >= 0; i--, key >>= 4)
            s[size_t(i)] = kHex[key & 15];
        return s;
    }
}

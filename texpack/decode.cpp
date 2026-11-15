// SpeedBreaker texture pack tools. GPL-3.0-or-later (see COPYING).
#include "decode.h"

#include <gpu/texture_layout.h>

#include <cstring>

namespace texpack
{
    namespace
    {
        uint32_t Le32(const uint8_t* p)
        {
            return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
        }

        // The untile shader's swapWord.
        uint32_t Swap(uint32_t v, uint32_t e)
        {
            if (e == 1)
                return ((v << 8) & 0xFF00FF00u) | ((v >> 8) & 0x00FF00FFu);
            if (e == 2)
                return (v << 24) | ((v << 8) & 0x00FF0000u) | ((v >> 8) & 0x0000FF00u) | (v >> 24);
            if (e == 3)
                return (v >> 16) | (v << 16);
            return v;
        }

        uint32_t Expand(uint32_t v, uint32_t bits)
        {
            const uint32_t m = (1u << bits) - 1;
            return (v * 255 + m / 2) / m;
        }

        void Color565(uint32_t c, uint32_t out[3])
        {
            // BC's own 565: red in the top bits.
            out[0] = Expand(c >> 11, 5);
            out[1] = Expand((c >> 5) & 63, 6);
            out[2] = Expand(c & 31, 5);
        }

        // One BC1-style colour block (8 host bytes) into a 4x4 RGBA tile.
        void ColorBlock(const uint8_t* b, bool fourColor, uint8_t tile[64])
        {
            const uint32_t c0 = b[0] | b[1] << 8, c1 = b[2] | b[3] << 8;
            uint32_t p[4][4];
            Color565(c0, p[0]);
            Color565(c1, p[1]);
            p[0][3] = p[1][3] = 255;
            if (fourColor || c0 > c1)
            {
                for (int k = 0; k < 3; k++)
                {
                    p[2][k] = (2 * p[0][k] + p[1][k] + 1) / 3;
                    p[3][k] = (p[0][k] + 2 * p[1][k] + 1) / 3;
                }
                p[2][3] = p[3][3] = 255;
            }
            else
            {
                for (int k = 0; k < 3; k++)
                {
                    p[2][k] = (p[0][k] + p[1][k]) / 2;
                    p[3][k] = 0;
                }
                p[2][3] = 255;
                p[3][3] = 0;
            }
            const uint32_t indices = Le32(b + 4);
            for (int i = 0; i < 16; i++)
            {
                const uint32_t* c = p[(indices >> (2 * i)) & 3];
                for (int k = 0; k < 4; k++)
                    tile[i * 4 + k] = uint8_t(c[k]);
            }
        }

        // BC2's explicit 4-bit alpha.
        void AlphaExplicit(const uint8_t* b, uint8_t tile[64])
        {
            for (int i = 0; i < 16; i++)
                tile[i * 4 + 3] = uint8_t(Expand((b[i / 2] >> (4 * (i & 1))) & 15, 4));
        }

        // BC3's interpolated alpha.
        void AlphaInterpolated(const uint8_t* b, uint8_t tile[64])
        {
            const uint32_t a0 = b[0], a1 = b[1];
            uint32_t a[8] = { a0, a1 };
            if (a0 > a1)
                for (int i = 1; i < 7; i++)
                    a[i + 1] = ((7 - i) * a0 + i * a1 + 3) / 7;
            else
            {
                for (int i = 1; i < 5; i++)
                    a[i + 1] = ((5 - i) * a0 + i * a1 + 2) / 5;
                a[6] = 0;
                a[7] = 255;
            }
            uint64_t bits = 0;
            for (int i = 0; i < 6; i++)
                bits |= uint64_t(b[2 + i]) << (8 * i);
            for (int i = 0; i < 16; i++)
                tile[i * 4 + 3] = uint8_t(a[(bits >> (3 * i)) & 7]);
        }
    }

    bool DecodeTexture(uint32_t format, uint32_t endian, uint32_t width, uint32_t height, std::span<const uint8_t> blocks,
        std::vector<uint8_t>& rgba)
    {
        gpu::texlayout::FormatInfo fi{};
        if (!gpu::texlayout::GetFormat(format, fi) || width == 0 || height == 0)
            return false;
        const bool bc = format == 18 || format == 19 || format == 20;
        const bool plain = format == 6 || format == 14;
        const bool packed16 = format == 3 || format == 4 || format == 15;
        if (!bc && !plain && !packed16)
            return false;
        const uint32_t bw = (width + fi.block - 1) / fi.block, bh = (height + fi.block - 1) / fi.block;
        const size_t bpb = size_t(1) << fi.bpbLog2;
        if (blocks.size() < size_t(bw) * bh * bpb)
            return false;
        rgba.assign(size_t(width) * height * 4, 0);

        if (packed16)
        {
            for (uint32_t y = 0; y < height; y++)
                for (uint32_t x = 0; x < width; x++)
                {
                    // Its word: this texel and its even/odd neighbour (one word, as in memory).
                    const size_t pair = (size_t(y) * bw + (x & ~1u)) * 2;
                    uint8_t word[4] = {};
                    memcpy(word, &blocks[pair], pair + 4 <= blocks.size() ? 4 : 2);
                    const uint32_t v = (Swap(Le32(word), endian) >> ((x & 1) * 16)) & 0xFFFF;
                    uint32_t r, g, b, a;
                    if (format == 4)
                        r = Expand(v & 31, 5), g = Expand((v >> 5) & 63, 6), b = Expand((v >> 11) & 31, 5), a = 255;
                    else if (format == 3)
                        r = Expand(v & 31, 5), g = Expand((v >> 5) & 31, 5), b = Expand((v >> 10) & 31, 5), a = (v >> 15) ? 255 : 0;
                    else
                        r = Expand(v & 15, 4), g = Expand((v >> 4) & 15, 4), b = Expand((v >> 8) & 15, 4), a = Expand(v >> 12, 4);
                    uint8_t* o = &rgba[(size_t(y) * width + x) * 4];
                    o[0] = uint8_t(r), o[1] = uint8_t(g), o[2] = uint8_t(b), o[3] = uint8_t(a);
                }
            return true;
        }

        // Host bytes: every word swapped.
        std::vector<uint8_t> host(size_t(bw) * bh * bpb);
        for (size_t i = 0; i + 4 <= host.size(); i += 4)
        {
            const uint32_t v = Swap(Le32(&blocks[i]), endian);
            host[i] = uint8_t(v), host[i + 1] = uint8_t(v >> 8), host[i + 2] = uint8_t(v >> 16), host[i + 3] = uint8_t(v >> 24);
        }
        if (plain)
        {
            memcpy(rgba.data(), host.data(), rgba.size());
            return true;
        }
        uint8_t tile[64];
        for (uint32_t by = 0; by < bh; by++)
            for (uint32_t bx = 0; bx < bw; bx++)
            {
                const uint8_t* b = &host[(size_t(by) * bw + bx) * bpb];
                if (format == 18)
                    ColorBlock(b, false, tile);
                else
                {
                    ColorBlock(b + 8, true, tile);
                    if (format == 19)
                        AlphaExplicit(b, tile);
                    else
                        AlphaInterpolated(b, tile);
                }
                for (uint32_t ty = 0; ty < 4; ty++)
                    for (uint32_t tx = 0; tx < 4; tx++)
                    {
                        const uint32_t x = bx * 4 + tx, y = by * 4 + ty;
                        if (x < width && y < height)
                            memcpy(&rgba[(size_t(y) * width + x) * 4], &tile[(ty * 4 + tx) * 4], 4);
                    }
            }
        return true;
    }
}

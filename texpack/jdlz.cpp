// SpeedBreaker texture pack tools. GPL-3.0-or-later (see COPYING).
#include "jdlz.h"

#include <cstring>

namespace texpack
{
    namespace
    {
        uint32_t Le32(const uint8_t* p)
        {
            return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
        }
        constexpr uint32_t kBlockMagic = 0x55441122;
    }

    bool JdlzDecompress(const uint8_t* in, size_t size, std::vector<uint8_t>& out)
    {
        out.clear();
        if (size < 16 || memcmp(in, "JDLZ", 4) != 0)
            return false;
        const uint32_t usize = Le32(in + 8);
        out.resize(usize);
        size_t ip = 16, op = 0;
        uint32_t flags1 = 1, flags2 = 1;
        while (op < usize)
        {
            if (flags1 == 1)
            {
                if (ip >= size)
                    return false;
                flags1 = in[ip++] | 0x100u;
            }
            if (flags2 == 1)
            {
                if (ip >= size)
                    return false;
                flags2 = in[ip++] | 0x100u;
            }
            if (flags1 & 1)
            {
                if (ip + 2 > size)
                    return false;
                uint32_t distance, length;
                if (flags2 & 1)
                {
                    length = (in[ip + 1] | ((in[ip] & 0xF0u) << 4)) + 3;
                    distance = (in[ip] & 0x0Fu) + 1;
                }
                else
                {
                    distance = (in[ip + 1] | ((in[ip] & 0xE0u) << 3)) + 17;
                    length = (in[ip] & 0x1Fu) + 3;
                }
                ip += 2;
                if (distance > op)
                    return false;
                for (uint32_t i = 0; i < length && op < usize; i++, op++)
                    out[op] = out[op - distance];
                flags2 >>= 1;
            }
            else
            {
                if (ip >= size)
                    return false;
                out[op++] = in[ip++];
            }
            flags1 >>= 1;
        }
        return true;
    }

    void DecodeBlocks(const uint8_t* file, size_t size, std::vector<Section>& sections, BlockStats& stats)
    {
        size_t at = 0;
        std::vector<uint8_t> out;
        while (at + 24 <= size)
        {
            if (Le32(file + at) != kBlockMagic)
            {
                at += 4;
                continue;
            }
            const uint32_t usize = Le32(file + at + 4), csize = Le32(file + at + 8), offset = Le32(file + at + 12);
            if (csize < 24 || at + csize > size)
                break;
            if (offset == 0 || sections.empty())
                sections.emplace_back();
            Section& s = sections.back();
            if (s.data.size() < size_t(offset) + usize)
            {
                s.data.resize(size_t(offset) + usize);
                s.known.resize(size_t(offset) + usize);
            }
            const uint8_t* payload = file + at + 24;
            if (csize >= 28 && memcmp(payload, "JDLZ", 4) == 0)
            {
                if (JdlzDecompress(payload, csize - 24, out) && out.size() == usize)
                {
                    memcpy(s.data.data() + offset, out.data(), usize);
                    std::fill(s.known.begin() + offset, s.known.begin() + offset + usize, true);
                    stats.jdlz++;
                }
                else
                    stats.failed++;
            }
            else
                stats.other++;
            at += csize;
        }
    }
}

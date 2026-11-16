// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING). See title_icon.h.
#include "title_icon.h"

#include <cstring>
#include <fstream>
#include <iterator>

#include <image.h>
#include <xex.h>

namespace install
{
    namespace
    {
        uint16_t U16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }
        uint32_t U32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
        uint64_t U64(const uint8_t* p) { return uint64_t(U32(p)) << 32 | U32(p + 4); }

        constexpr uint16_t kImageNamespace = 2;
        constexpr size_t kXdbfHeader = 24, kXdbfEntry = 18, kXdbfFree = 8;
        constexpr uint8_t kPng[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    }

    std::vector<uint8_t> XdbfImage(std::span<const uint8_t> xdbf, uint64_t id)
    {
        // Header: "XDBF", version, entry table length, entries used, free
        // table length, free entries used (u32 each, big-endian). Then the
        // entry table (namespace u16, id u64, offset u32, length u32), the
        // free table (offset u32, length u32), and the data the offsets count from.
        if (xdbf.size() < kXdbfHeader || std::memcmp(xdbf.data(), "XDBF", 4) != 0)
            return {};
        const uint64_t entries = U32(&xdbf[8]), used = U32(&xdbf[12]), freeEntries = U32(&xdbf[16]);
        const uint64_t dataStart = kXdbfHeader + entries * kXdbfEntry + freeEntries * kXdbfFree;
        if (used > entries || dataStart > xdbf.size())
            return {};
        for (uint64_t i = 0; i < used; i++)
        {
            const uint8_t* e = &xdbf[kXdbfHeader + i * kXdbfEntry];
            if (U16(e) != kImageNamespace || U64(e + 2) != id)
                continue;
            const uint64_t offset = U32(e + 10), length = U32(e + 14);
            if (offset > xdbf.size() - dataStart || length > xdbf.size() - dataStart - offset)
                return {};
            const uint8_t* data = &xdbf[dataStart + offset];
            if (length < sizeof(kPng) || std::memcmp(data, kPng, sizeof(kPng)) != 0)
                return {};
            return std::vector<uint8_t>(data, data + length);
        }
        return {};
    }

    std::vector<uint8_t> TitleIcon(std::span<const uint8_t> xexFile, std::span<const uint8_t> image, uint64_t imageBase)
    {
        // The optional header table: "XEX2", flags, PE offset, reserved,
        // security offset, header count (u32 each), then (key, value) pairs.
        if (xexFile.size() < 24 || std::memcmp(xexFile.data(), "XEX2", 4) != 0)
            return {};
        const uint64_t headers = U32(&xexFile[20]);
        if (headers > (xexFile.size() - 24) / 8)
            return {};
        for (uint64_t h = 0; h < headers; h++)
        {
            const uint8_t* entry = &xexFile[24 + h * 8];
            if (U32(entry) != XEX_HEADER_RESOURCE_INFO)
                continue;
            // [u32 size, size included] then 16-byte resources: an 8-character
            // name (the title id in hex for the title's own), address, size.
            const uint64_t at = U32(entry + 4);
            if (at > xexFile.size() - 4)
                return {};
            const uint64_t size = U32(&xexFile[at]);
            if (size < 4 || size > xexFile.size() - at)
                return {};
            for (uint64_t r = at + 4; r + 16 <= at + size; r += 16)
            {
                const uint64_t address = U32(&xexFile[r + 8]), length = U32(&xexFile[r + 12]);
                if (address < imageBase || address - imageBase > image.size() || length > image.size() - (address - imageBase))
                    continue;
                std::vector<uint8_t> icon = XdbfImage(image.subspan(address - imageBase, length), kTitleIconId);
                if (!icon.empty())
                    return icon;
            }
            return {};
        }
        return {};
    }

    std::vector<uint8_t> TitleIconFromXex(const std::filesystem::path& xex)
    {
        std::ifstream f(xex, std::ios::binary);
        std::vector<uint8_t> file((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (!f.eof() && f.fail())
            return {};
        if (file.size() < 24 || std::memcmp(file.data(), "XEX2", 4) != 0)
            return {};
        Image loaded = Xex2LoadImage(file.data(), file.size());
        if (!loaded.data || loaded.size == 0)
            return {};
        return TitleIcon(file, std::span<const uint8_t>(loaded.data.get(), loaded.size), loaded.base);
    }
}

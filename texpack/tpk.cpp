// SpeedBreaker texture pack tools. GPL-3.0-or-later (see COPYING).
#include "tpk.h"

#include <cstring>

namespace texpack
{
    namespace
    {
        constexpr uint32_t kTpk = 0xB3300000, kInfo = 0xB3310000, kHeader = 0x33310001, kRecords = 0x33310004,
                           kFetch = 0x33310005, kData = 0xB3320000, kPixels = 0x33320003;
        constexpr size_t kRecordSize = 124, kFetchSize = 32;

        uint32_t Le32(const uint8_t* p)
        {
            return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
        }
        uint32_t Le16(const uint8_t* p)
        {
            return uint32_t(p[0]) | uint32_t(p[1]) << 8;
        }
        std::string Text(const uint8_t* p, size_t max)
        {
            size_t n = strnlen(reinterpret_cast<const char*>(p), max);
            return std::string(reinterpret_cast<const char*>(p), n);
        }

        // Calls `each(id, payload, size)` for the chunks in [at, end); stops
        // at one that runs past `end`.
        template<typename F>
        void Chunks(std::span<const uint8_t> f, size_t at, size_t end, F&& each)
        {
            while (at + 8 <= end)
            {
                const uint32_t id = Le32(&f[at]), size = Le32(&f[at + 4]);
                if (at + 8 + size_t(size) > end)
                    return;
                each(id, at + 8, size_t(size));
                at += 8 + size_t(size);
            }
        }

        void Find(std::span<const uint8_t> f, size_t at, size_t end, int depth, std::vector<TpkSpan>& out)
        {
            Chunks(f, at, end, [&](uint32_t id, size_t payload, size_t size) {
                if (id == kTpk)
                    out.push_back({ payload - 8, payload + size });
                else if ((id & 0x80000000u) && depth < 4)
                    Find(f, payload, payload + size, depth + 1, out);
            });
        }

        // Levels of a full chain down to 1x1, when the data holds more than
        // the base level.
        uint32_t Levels(uint32_t width, uint32_t height, uint32_t baseSize, uint32_t dataSize)
        {
            if (dataSize <= baseSize)
                return 1;
            uint32_t levels = 1;
            for (uint32_t s = width > height ? width : height; s > 1; s >>= 1)
                levels++;
            return levels;
        }
    }

    void FindTpks(std::span<const uint8_t> file, std::vector<TpkSpan>& out)
    {
        Find(file, 0, file.size(), 0, out);
    }

    void ScanTpks(std::span<const uint8_t> f, std::vector<TpkSpan>& out)
    {
        for (size_t at = 0; at + 16 <= f.size(); at += 4)
        {
            if (Le32(&f[at]) != kTpk)
                continue;
            const size_t end = at + 8 + size_t(Le32(&f[at + 4]));
            if (end > f.size())
                continue;
            bool info = false, data = false;
            Chunks(f, at + 8, end, [&](uint32_t id, size_t, size_t) {
                info = info || id == kInfo;
                data = data || id == kData;
            });
            if (info && data)
            {
                out.push_back({ at, end });
                at = end - 4;  // (TPKs don't nest)
            }
        }
    }

    bool ParseTpk(std::span<const uint8_t> f, const TpkSpan& tpk, std::vector<TexRecord>& out, std::string& error)
    {
        size_t records = 0, recordsSize = 0, fetch = 0, fetchSize = 0, header = 0, pixels = 0;
        Chunks(f, tpk.start + 8, tpk.end, [&](uint32_t id, size_t payload, size_t size) {
            if (id == kInfo)
                Chunks(f, payload, payload + size, [&](uint32_t id2, size_t p2, size_t s2) {
                    if (id2 == kHeader)
                        header = p2;
                    else if (id2 == kRecords)
                        records = p2, recordsSize = s2;
                    else if (id2 == kFetch)
                        fetch = p2, fetchSize = s2;
                });
            else if (id == kData)
                Chunks(f, payload, payload + size, [&](uint32_t id2, size_t p2, size_t) {
                    if (id2 == kPixels)
                        pixels = p2;
                });
        });
        if (!records || !fetch || !pixels)
        {
            error = "a TPK without texture records, fetch templates or pixel data";
            return false;
        }
        const size_t count = recordsSize / kRecordSize;
        if (count * kRecordSize != recordsSize || fetchSize < count * kFetchSize)
        {
            error = "a TPK whose records don't add up";
            return false;
        }
        const std::string tpkName = header && header + 0x20 + 64 <= f.size() ? Text(&f[header + 0x20], 64) : std::string();
        const size_t dataStart = (pixels + 0xFFF) & ~size_t(0xFFF);
        for (size_t i = 0; i < count; i++)
        {
            const uint8_t* r = &f[records + i * kRecordSize];
            const uint32_t dword1 = Le32(&f[fetch + i * kFetchSize + 0x14]);
            TexRecord t;
            t.name = Text(r + 0xC, 24);
            t.tpkName = tpkName;
            t.nameHash = Le32(r + 0x24);
            t.dataOffset = dataStart + Le32(r + 0x30);
            t.dataSize = Le32(r + 0x38);
            t.baseSize = Le32(r + 0x40);
            t.width = Le16(r + 0x44);
            t.height = Le16(r + 0x46);
            t.format = dword1 & 0x3F;
            t.endian = (dword1 >> 6) & 3;
            t.mipCount = Levels(t.width, t.height, t.baseSize, t.dataSize);
            out.push_back(std::move(t));
        }
        return true;
    }
}

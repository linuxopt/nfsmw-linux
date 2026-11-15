// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
#include "texpack.h"
#include "texpack_key.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace gpu::texpack
{
    namespace
    {
        uint32_t Le32(const uint8_t* p)
        {
            return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
        }

        // pack.json's "region" (written by speedbreaker-texpack; a flat object).
        std::string Region(const std::string& json)
        {
            const size_t at = json.find("\"region\"");
            if (at == std::string::npos)
                return "";
            const size_t open = json.find('"', json.find(':', at) + 1);
            const size_t close = open == std::string::npos ? open : json.find('"', open + 1);
            return close == std::string::npos ? "" : json.substr(open + 1, close - open - 1);
        }
    }

    bool Pack::Open(const std::filesystem::path& dir, std::string_view region, std::string& why)
    {
        m_dir = dir;
        m_keys.clear();
        std::ifstream f(dir / "pack.json");
        if (!f)
        {
            why = "no pack.json in " + dir.string();
            return false;
        }
        std::stringstream ss;
        ss << f.rdbuf();
        const std::string packRegion = Region(ss.str());
        if (packRegion != region)
        {
            why = "the pack in " + dir.string() + " was built for the " + (packRegion.empty() ? "unknown" : packRegion) +
                " disc, not " + std::string(region);
            return false;
        }
        std::error_code ec;
        for (const auto& e : std::filesystem::directory_iterator(dir, ec))
        {
            const std::string name = e.path().filename().string();
            if (name.size() != 20 || e.path().extension() != ".dds")
                continue;
            const std::string stem = name.substr(0, 16);
            if (stem.find_first_not_of("0123456789abcdef") != std::string::npos)
                continue;
            m_keys.insert(std::stoull(stem, nullptr, 16));
        }
        return true;
    }

    bool Pack::Load(uint64_t key, uint32_t capPx, Image& out, std::string& why) const
    {
        out = Image();
        if (!Has(key))
        {
            why = "not in the pack";
            return false;
        }
        const std::filesystem::path path = m_dir / (KeyName(key) + ".dds");
        std::ifstream f(path, std::ios::binary);
        std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (d.size() < 148 || memcmp(d.data(), "DDS ", 4) != 0 || memcmp(&d[84], "DX10", 4) != 0 || Le32(&d[128]) != 98)
        {
            why = path.string() + ": not a BC7 DDS";
            return false;
        }
        const uint32_t h = Le32(&d[12]), w = Le32(&d[16]), levels = std::max(1u, Le32(&d[28]));
        if (w == 0 || h == 0 || w > 16384 || h > 16384 || levels > 15)
        {
            why = path.string() + ": a bad size";
            return false;
        }
        // Every level's place; the file must hold them all.
        std::vector<size_t> offsets;
        size_t at = 148;
        for (uint32_t l = 0; l < levels; l++)
        {
            offsets.push_back(at);
            const uint32_t lw = std::max(1u, w >> l), lh = std::max(1u, h >> l);
            at += size_t((lw + 3) / 4) * ((lh + 3) / 4) * 16;
        }
        if (d.size() < at)
        {
            why = path.string() + ": shorter than its levels (an interrupted copy?)";
            return false;
        }
        uint32_t first = 0;
        while (first + 1 < levels && std::max(w >> first, h >> first) > capPx)
            first++;
        out.width = std::max(1u, w >> first);
        out.height = std::max(1u, h >> first);
        out.levels = levels - first;
        out.data.assign(d.begin() + std::ptrdiff_t(offsets[first]), d.begin() + std::ptrdiff_t(at));
        for (uint32_t l = first; l < levels; l++)
            out.levelOffsets.push_back(offsets[l] - offsets[first]);
        return true;
    }
}

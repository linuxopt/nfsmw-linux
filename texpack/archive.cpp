// SpeedBreaker texture pack tools. GPL-3.0-or-later (see COPYING).
#include "archive.h"

#include <fstream>

namespace texpack
{
    namespace
    {
        uint32_t Le32(const uint8_t* p)
        {
            return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
        }
    }

    bool Archive::Open(const std::filesystem::path& nfsDir, std::string& error)
    {
        m_dir = nfsDir;
        m_entries.clear();
        std::ifstream f(nfsDir / "ZDIR.BIN", std::ios::binary);
        if (!f)
        {
            error = "no ZDIR.BIN in " + nfsDir.string();
            return false;
        }
        std::vector<uint8_t> zdir((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (zdir.empty() || zdir.size() % 24 != 0)
        {
            error = "ZDIR.BIN is " + std::to_string(zdir.size()) + " bytes, not whole 24-byte entries";
            return false;
        }
        for (size_t at = 0; at < zdir.size(); at += 24)
            m_entries.push_back({ Le32(&zdir[at]), Le32(&zdir[at + 4]), Le32(&zdir[at + 8]), Le32(&zdir[at + 16]) });
        return true;
    }

    bool Archive::Read(const ArchiveEntry& entry, std::vector<uint8_t>& out) const
    {
        out.clear();
        const std::filesystem::path path = m_dir / ("ZZDATA" + std::to_string(entry.archive) + ".BIN");
        std::error_code ec;
        const uint64_t fileSize = std::filesystem::file_size(path, ec);
        const uint64_t offset = uint64_t(entry.sector) * 0x800;
        if (ec || offset + entry.size > fileSize)
            return false;
        std::ifstream f(path, std::ios::binary);
        f.seekg(std::streamoff(offset));
        out.resize(entry.size);
        if (!f.read(reinterpret_cast<char*>(out.data()), std::streamsize(entry.size)))
        {
            out.clear();
            return false;
        }
        return true;
    }
}

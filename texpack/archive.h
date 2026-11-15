// SpeedBreaker texture pack tools. GPL-3.0-or-later (see COPYING).
//
// The disc's file archive: NFS/ZDIR.BIN lists every file as a 24-byte
// little-endian entry {name hash, archive, sector, size, 2 words unused
// here}, sorted by hash (EA's string hash of the path: game/language.h
// ArchiveHash). A file's bytes are at ZZDATA<archive>.BIN, offset
// sector * 0x800. The index names some files beyond their archive's end;
// those are skipped (Read is false), never fatal.
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace texpack
{
    struct ArchiveEntry
    {
        uint32_t nameHash, archive, sector, size;
    };

    class Archive
    {
    public:
        // `nfsDir`: the install's NFS folder (ZDIR.BIN, ZZDATA0-9.BIN).
        bool Open(const std::filesystem::path& nfsDir, std::string& error);
        const std::vector<ArchiveEntry>& Entries() const { return m_entries; }
        bool Read(const ArchiveEntry& entry, std::vector<uint8_t>& out) const;

    private:
        std::filesystem::path m_dir;
        std::vector<ArchiveEntry> m_entries;
    };
}

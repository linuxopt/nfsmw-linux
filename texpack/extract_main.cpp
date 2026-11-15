// SpeedBreaker texture pack tools. GPL-3.0-or-later (see COPYING).
//
// texpack-extract: the textures of the player's installed disc.
//
//   texpack-extract --keys-only <install>/NFS
//
// prints one line per unique texture: its key (gpu/texpack_key.h), the TPK
// it came from, its Xenos format and size; and a summary on stderr. Files
// stored in compressed blocks (the city stream) are decoded first; a
// texture whose bytes sit in a block this can't decode yet (HUFF) is
// counted, not keyed.
#include "archive.h"
#include "jdlz.h"
#include "tpk.h"

#include <gpu/texpack_key.h>
#include <gpu/texture_layout.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_set>

namespace
{
    struct Counts
    {
        uint32_t files = 0, tpks = 0, tpksUndecoded = 0, textures = 0, keyed = 0, unique = 0, undecoded = 0, unsupported = 0, outside = 0;
    };

    // `known`: per byte decoded (empty: all of it).
    void KeyTextures(std::span<const uint8_t> file, const std::vector<bool>& known, Counts& c, std::unordered_set<uint64_t>& seen)
    {
        std::vector<texpack::TpkSpan> tpks;
        texpack::FindTpks(file, tpks);
        std::vector<texpack::TexRecord> recs;
        std::vector<uint8_t> blocks;
        std::string error;
        for (const texpack::TpkSpan& t : tpks)
        {
            // Its records (the 0xB3310000 chunk) must be decoded; its pixels
            // are checked per texture below.
            if (!known.empty())
            {
                bool recordsKnown = false;
                for (size_t at = t.start + 8; at + 8 <= t.end;)
                {
                    if (!known[at] || !known[at + 7])
                        break;
                    const uint32_t id = file[at] | file[at + 1] << 8 | file[at + 2] << 16 | uint32_t(file[at + 3]) << 24;
                    const uint32_t size = file[at + 4] | file[at + 5] << 8 | file[at + 6] << 16 | uint32_t(file[at + 7]) << 24;
                    if (at + 8 + size_t(size) > t.end)
                        break;
                    if (id == 0xB3310000)
                    {
                        recordsKnown = true;
                        for (size_t i = at; recordsKnown && i < at + 8 + size; i++)
                            recordsKnown = known[i];
                        break;
                    }
                    at += 8 + size_t(size);
                }
                if (!recordsKnown)
                {
                    c.tpksUndecoded++;
                    continue;
                }
            }
            recs.clear();
            if (!texpack::ParseTpk(file, t, recs, error))
                continue;
            c.tpks++;
            for (const texpack::TexRecord& r : recs)
            {
                c.textures++;
                gpu::texlayout::FormatInfo fi{};
                if (!gpu::texlayout::GetFormat(r.format, fi) || r.width == 0 || r.height == 0)
                {
                    c.unsupported++;
                    continue;
                }
                if (!texpack::InFile(r, file.size()))
                {
                    c.outside++;
                    continue;
                }
                bool decoded = true;
                for (size_t i = r.dataOffset; decoded && i < r.dataOffset + r.baseSize; i += 256)
                    decoded = known.empty() || known[i];
                if (!decoded || (!known.empty() && !known[r.dataOffset + r.baseSize - 1]))
                {
                    c.undecoded++;
                    continue;
                }
                const uint32_t pitch = (r.width + fi.block - 1) / fi.block;
                gpu::texlayout::GatherBase(fi, r.width, r.height, pitch, r.tiled, file.data() + r.dataOffset, blocks);
                const uint64_t key = gpu::texpack::Key(blocks.data(), blocks.size(), r.format, r.width, r.height);
                c.keyed++;
                if (!seen.insert(key).second)
                    continue;
                c.unique++;
                printf("%s %s %u %ux%u\n", gpu::texpack::KeyName(key).c_str(), r.tpkName.empty() ? "-" : r.tpkName.c_str(),
                    r.format, r.width, r.height);
            }
        }
    }
}

int main(int argc, char** argv)
{
    if (argc != 3 || strcmp(argv[1], "--keys-only") != 0)
    {
        fprintf(stderr, "usage: texpack-extract --keys-only <install>/NFS\n");
        return 2;
    }
    texpack::Archive archive;
    std::string error;
    if (!archive.Open(argv[2], error))
    {
        fprintf(stderr, "texpack-extract: %s\n", error.c_str());
        return 1;
    }
    Counts c;
    texpack::BlockStats blockStats;
    std::unordered_set<uint64_t> seen;
    std::vector<uint8_t> file;
    const std::vector<bool> all;
    for (const texpack::ArchiveEntry& e : archive.Entries())
    {
        if (e.size == 0 || !archive.Read(e, file))
            continue;
        c.files++;
        if (file.size() >= 4 && file[0] == 0x22 && file[1] == 0x11 && file[2] == 0x44 && file[3] == 0x55)
        {
            std::vector<texpack::Section> sections;
            texpack::DecodeBlocks(file.data(), file.size(), sections, blockStats);
            for (const texpack::Section& s : sections)
                KeyTextures(s.data, s.known, c, seen);
        }
        else
            KeyTextures(file, all, c, seen);
    }
    fprintf(stderr,
        "texpack-extract: %u files, %u TPKs (%u more with records not decoded yet), %u textures: %u keyed (%u unique), %u in blocks not decoded yet (HUFF), "
        "%u in unsupported formats, %u outside their file; blocks: %u JDLZ, %u other, %u failed\n",
        c.files, c.tpks, c.tpksUndecoded, c.textures, c.keyed, c.unique, c.undecoded, c.unsupported, c.outside, blockStats.jdlz, blockStats.other,
        blockStats.failed);
    return 0;
}

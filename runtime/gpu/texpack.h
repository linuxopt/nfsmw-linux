// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// The HD texture pack as the game reads it: <data>/texture-pack/, made by
// speedbreaker-texpack (texpack/), one <key>.dds per replacement (BC7, DX10
// header, full mip chain; the key is gpu/texpack_key.h's) and pack.json
// (its disc region). Open indexes the folder by file name, without opening
// the files; Load reads one replacement, starting at the first mip level
// whose long side fits `capPx` (the machine's tier, gpu/texpack_auto.h), and
// refuses a file shorter than its header says (an interrupted copy). A pack
// built for another disc region is ignored. Vulkan-free, for
// tests/texpack_pack_test.cpp.
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace gpu::texpack
{
    class Pack
    {
    public:
        struct Image
        {
            uint32_t width = 0, height = 0, levels = 0;
            std::vector<uint8_t> data;          // BC7 blocks, level after level
            std::vector<size_t> levelOffsets;   // each level's first byte in data
        };

        bool Open(const std::filesystem::path& dir, std::string_view region, std::string& why);
        bool Has(uint64_t key) const { return m_keys.count(key) != 0; }
        bool Load(uint64_t key, uint32_t capPx, Image& out, std::string& why) const;
        size_t Count() const { return m_keys.size(); }
        const std::filesystem::path& Dir() const { return m_dir; }

    private:
        std::filesystem::path m_dir;
        std::unordered_set<uint64_t> m_keys;
    };
}

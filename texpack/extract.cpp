// SpeedBreaker texture pack tools. GPL-3.0-or-later (see COPYING).
#include "extract.h"
#include "archive.h"
#include "classify.h"
#include "decode.h"
#include "jdlz.h"
#include "tpk.h"

#include <gpu/texpack_key.h>
#include <gpu/texture_layout.h>

#include <algorithm>
#include <atomic>
#include <fstream>
#include <map>
#include <mutex>
#include <span>
#include <sstream>
#include <thread>
#include <vector>

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_ASSERT(x) ((void)0)
#include "thirdparty/stb/stb_image_write.h"

namespace texpack
{
    namespace
    {
        struct Item
        {
            uint64_t key;
            uint32_t format, endian, width, height;
            std::string name, tpk;
            bool fromCapture;
            std::vector<uint8_t> blocks;
            TexClass cls;
            std::string alpha = "none";
        };

        uint32_t Le32(const uint8_t* p)
        {
            return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
        }

        // The records chunk of the TPK at `t` is wholly decoded.
        bool RecordsKnown(std::span<const uint8_t> f, const std::vector<bool>& known, const TpkSpan& t)
        {
            if (known.empty())
                return true;
            for (size_t at = t.start + 8; at + 8 <= t.end;)
            {
                if (!known[at] || !known[at + 7])
                    return false;
                const uint32_t id = Le32(&f[at]), size = Le32(&f[at + 4]);
                if (at + 8 + size_t(size) > t.end)
                    return false;
                if (id == 0xB3310000)
                {
                    for (size_t i = at; i < at + 8 + size; i++)
                        if (!known[i])
                            return false;
                    return true;
                }
                at += 8 + size_t(size);
            }
            return false;
        }

        void FromFile(std::span<const uint8_t> f, const std::vector<bool>& known, std::map<uint64_t, Item>& items, ExtractStats& st)
        {
            std::vector<TpkSpan> tpks;
            ScanTpks(f, tpks);
            std::vector<TexRecord> recs;
            std::string error;
            for (const TpkSpan& t : tpks)
            {
                recs.clear();
                if (!RecordsKnown(f, known, t) || !ParseTpk(f, t, recs, error))
                    continue;
                st.tpks++;
                for (const TexRecord& r : recs)
                {
                    st.textures++;
                    gpu::texlayout::FormatInfo fi{};
                    if (!gpu::texlayout::GetFormat(r.format, fi) || r.width == 0 || r.height == 0 || !InFile(r, f.size()))
                        continue;
                    bool decoded = true;
                    for (size_t i = r.dataOffset; decoded && !known.empty() && i < r.dataOffset + r.baseSize; i += 64)
                        decoded = known[i];
                    if (!decoded || (!known.empty() && !known[r.dataOffset + r.baseSize - 1]))
                    {
                        st.undecoded++;
                        continue;
                    }
                    Item it{ 0, r.format, r.endian, r.width, r.height, r.name, r.tpkName, false, {}, TexClass::Data };
                    gpu::texlayout::GatherBase(fi, r.width, r.height, (r.width + fi.block - 1) / fi.block, r.tiled, f.data() + r.dataOffset,
                        it.blocks);
                    it.key = gpu::texpack::Key(it.blocks.data(), it.blocks.size(), r.format, r.width, r.height);
                    items.emplace(it.key, std::move(it));
                }
            }
        }

        uint32_t JsonNumber(const std::string& json, const char* field)
        {
            const std::string want = std::string("\"") + field + "\":";
            size_t at = json.find(want);
            return at == std::string::npos ? 0 : uint32_t(std::stoul(json.substr(at + want.size())));
        }

        std::string JsonText(const std::string& s)
        {
            std::string out;
            for (char c : s)
            {
                if (c == '"' || c == '\\')
                    out += '\\';
                if (uint8_t(c) >= 0x20)
                    out += c;
            }
            return out;
        }

        const char* ClassName(TexClass c)
        {
            switch (c)
            {
            case TexClass::Color: return "color";
            case TexClass::Ui: return "ui";
            case TexClass::Normal: return "normal";
            default: return "data";
            }
        }
    }

    bool Extract(const ExtractOptions& o, ExtractStats& st, std::string& error)
    {
        Archive archive;
        if (!archive.Open(o.nfsDir, error))
            return false;
        std::map<uint64_t, Item> items;
        std::vector<uint8_t> file;
        const std::vector<bool> all;
        BlockStats blockStats;
        for (const ArchiveEntry& e : archive.Entries())
        {
            if (e.size == 0 || !archive.Read(e, file))
                continue;
            if (file.size() >= 4 && Le32(file.data()) == 0x55441122)
            {
                std::vector<Section> sections;
                DecodeBlocks(file.data(), file.size(), sections, blockStats);
                for (const Section& s : sections)
                    FromFile(s.data, s.known, items, st);
            }
            else
                FromFile(file, all, items, st);
        }
        st.jdlzBlocks = blockStats.jdlz;
        st.otherBlocks = blockStats.other;
        st.fromDisc = uint32_t(items.size());

        // Play-captured textures the disc didn't give.
        std::error_code ec;
        if (!o.captureDir.empty() && std::filesystem::is_directory(o.captureDir, ec))
            for (const auto& e : std::filesystem::directory_iterator(o.captureDir, ec))
            {
                if (e.path().extension() != ".blk" || e.path().stem().string().size() != 16)
                    continue;
                const uint64_t key = std::stoull(e.path().stem().string(), nullptr, 16);
                std::ifstream j(std::filesystem::path(e.path()).replace_extension(".json"));
                if (!j || items.count(key))
                    continue;
                std::stringstream ss;
                ss << j.rdbuf();
                std::string json = ss.str();
                json.erase(std::remove(json.begin(), json.end(), ' '), json.end());
                Item it{ key, JsonNumber(json, "format"), JsonNumber(json, "endian"), JsonNumber(json, "width"), JsonNumber(json, "height"),
                    "", "", true, {}, TexClass::Data };
                std::ifstream b(e.path(), std::ios::binary);
                it.blocks.assign(std::istreambuf_iterator<char>(b), {});
                items.emplace(key, std::move(it));
                st.fromCapture++;
            }
        st.unique = uint32_t(items.size());

        if (o.keysOnly)
        {
            for (const auto& [key, it] : items)
                fprintf(o.keysOnly, "%s %s %u %ux%u\n", gpu::texpack::KeyName(key).c_str(), it.tpk.empty() ? "-" : it.tpk.c_str(), it.format,
                    it.width, it.height);
            return true;
        }

        // Classify, decode and write the PNGs, in parallel.
        std::filesystem::create_directories(o.workDir / "src", ec);
        std::vector<Item*> list;
        for (auto& [key, it] : items)
            list.push_back(&it);
        std::atomic<size_t> next{ 0 };
        std::atomic<uint32_t> pngs{ 0 };
        auto work = [&] {
            std::vector<uint8_t> rgba;
            for (size_t i; (i = next++) < list.size();)
            {
                Item& it = *list[i];
                it.cls = Classify(it.format, it.width, it.height, it.name, it.tpk);
                if (it.cls != TexClass::Color && it.cls != TexClass::Ui)
                    continue;
                if (!DecodeTexture(it.format, it.endian, it.width, it.height, it.blocks, rgba))
                {
                    it.cls = TexClass::Data;
                    continue;
                }
                if (LooksLikeNormalMap(rgba))
                {
                    it.cls = TexClass::Normal;
                    continue;
                }
                bool opaque = true, binary = true;
                for (size_t p = 3; p < rgba.size(); p += 4)
                {
                    opaque = opaque && rgba[p] == 255;
                    binary = binary && (rgba[p] == 0 || rgba[p] == 255);
                }
                it.alpha = opaque ? "none" : binary ? "1bit" : "full";
                const std::filesystem::path png = o.workDir / "src" / (gpu::texpack::KeyName(it.key) + ".png");
                if (std::filesystem::exists(png))
                    continue;
                const std::filesystem::path partial = std::filesystem::path(png).concat(".partial");
                if (stbi_write_png(partial.c_str(), int(it.width), int(it.height), 4, rgba.data(), int(it.width * 4)))
                {
                    std::error_code e2;
                    std::filesystem::rename(partial, png, e2);
                    if (!e2)
                        pngs++;
                }
            }
        };
        const unsigned jobs = o.jobs ? o.jobs : std::max(1u, std::thread::hardware_concurrency());
        std::vector<std::thread> threads;
        for (unsigned t = 0; t < jobs; t++)
            threads.emplace_back(work);
        for (auto& t : threads)
            t.join();
        st.pngs = pngs;

        std::ofstream m(o.workDir / "manifest.json.partial");
        m << "{\"region\": \"" << JsonText(o.region) << "\", \"textures\": [\n";
        bool first = true;
        for (const auto& [key, it] : items)
        {
            switch (it.cls)
            {
            case TexClass::Color: st.color++; break;
            case TexClass::Ui: st.ui++; break;
            case TexClass::Normal: st.normal++; break;
            default: st.data++; break;
            }
            m << (first ? "" : ",\n") << "{\"key\": \"" << gpu::texpack::KeyName(key) << "\", \"class\": \"" << ClassName(it.cls)
              << "\", \"source\": \"" << (it.fromCapture ? "capture" : "disc") << "\", \"format\": " << it.format << ", \"width\": "
              << it.width << ", \"height\": " << it.height << ", \"alpha\": \"" << it.alpha << "\", \"tpk\": \"" << JsonText(it.tpk)
              << "\", \"name\": \"" << JsonText(it.name) << "\"}";
            first = false;
        }
        m << "\n]}\n";
        m.close();
        std::filesystem::rename(o.workDir / "manifest.json.partial", o.workDir / "manifest.json", ec);
        if (ec)
        {
            error = "can't write " + (o.workDir / "manifest.json").string();
            return false;
        }
        return true;
    }
}

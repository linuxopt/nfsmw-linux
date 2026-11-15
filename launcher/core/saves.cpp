// SpeedBreaker desktop launcher. GPL-3.0-or-later (see COPYING). See saves.h.
#include "saves.h"

#include <algorithm>
#include <fstream>
#include <iterator>

namespace launcher::saves
{
    namespace fs = std::filesystem;

    bool IsFolder(std::string_view folder)
    {
        return std::find(std::begin(kFolders), std::end(kFolders), folder) != std::end(kFolders);
    }

    const char* ReleaseLabel(std::string_view folder)
    {
        if (folder == "save-pal-es")
            return "Europe (Spanish)";
        if (folder == "save-pal-de")
            return "Europe (German)";
        return "USA";
    }

    bool SafeName(std::string_view name)
    {
        return !name.empty() && name.size() <= 42 && name[0] != '.' && name.find_first_of("/\\:") == std::string_view::npos &&
               name.find('\0') == std::string_view::npos;
    }

    std::string DisplayName(std::span<const uint8_t> x)
    {
        std::string out;
        if (x.size() < 8)
            return out;
        const size_t units = std::min<size_t>(128, (x.size() - 8) / 2);
        auto unit = [&](size_t i) { return char16_t((x[8 + 2 * i] << 8) | x[8 + 2 * i + 1]); };
        for (size_t i = 0; i < units; i++)
        {
            uint32_t c = unit(i);
            if (c == 0)
                break;
            if (c >= 0xD800 && c < 0xDC00 && i + 1 < units)
            {
                uint32_t lo = unit(i + 1);
                if (lo >= 0xDC00 && lo < 0xE000)
                {
                    c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
                    i++;
                }
            }
            if (c >= 0xD800 && c < 0xE000)
                c = 0xFFFD;  // a lone surrogate
            if (c < 0x80)
                out += char(c);
            else if (c < 0x800)
            {
                out += char(0xC0 | (c >> 6));
                out += char(0x80 | (c & 0x3F));
            }
            else if (c < 0x10000)
            {
                out += char(0xE0 | (c >> 12));
                out += char(0x80 | ((c >> 6) & 0x3F));
                out += char(0x80 | (c & 0x3F));
            }
            else
            {
                out += char(0xF0 | (c >> 18));
                out += char(0x80 | ((c >> 12) & 0x3F));
                out += char(0x80 | ((c >> 6) & 0x3F));
                out += char(0x80 | (c & 0x3F));
            }
        }
        return out;
    }

    std::vector<Profile> List(const fs::path& userDir)
    {
        std::vector<Profile> out;
        for (const char* folder : kFolders)
        {
            std::vector<Profile> here;
            std::error_code ec;
            for (fs::directory_iterator it(userDir / folder, ec), end; !ec && it != end; it.increment(ec))
            {
                const fs::path& p = it->path();
                if (p.extension() != ".xcontent" || !it->is_regular_file(ec))
                    continue;
                const std::string name = p.stem().string();
                if (!SafeName(name) || !fs::is_directory(userDir / folder / name, ec))
                    continue;
                std::ifstream f(p, std::ios::binary);
                std::string bytes(std::istreambuf_iterator<char>(f), {});
                if (bytes.size() < kXcontentSize)
                    continue;
                const auto* b = reinterpret_cast<const uint8_t*>(bytes.data());
                const uint32_t type = (uint32_t(b[4]) << 24) | (uint32_t(b[5]) << 16) | (uint32_t(b[6]) << 8) | b[7];
                if (type != 1)
                    continue;
                Profile profile{ folder, name, DisplayName({ b, bytes.size() }), {} };
                fs::path dir = userDir / folder / name;
                profile.lastPlayed = fs::last_write_time(dir, ec);
                bool any = false;
                for (fs::directory_iterator file(dir, ec), fend; !ec && file != fend; file.increment(ec))
                {
                    std::error_code fec;
                    if (!file->is_regular_file(fec))
                        continue;
                    auto t = file->last_write_time(fec);
                    if (!fec && (!any || t > profile.lastPlayed))
                    {
                        profile.lastPlayed = t;
                        any = true;
                    }
                }
                here.push_back(std::move(profile));
            }
            std::sort(here.begin(), here.end(), [](const Profile& a, const Profile& b) { return a.name < b.name; });
            out.insert(out.end(), here.begin(), here.end());
        }
        return out;
    }
}

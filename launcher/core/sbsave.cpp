// SpeedBreaker desktop launcher. GPL-3.0-or-later (see COPYING). See sbsave.h.
#include "sbsave.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <set>
#include <string_view>
#include <vector>

#include <unistd.h>

namespace launcher::sbsave
{
    namespace fs = std::filesystem;
    namespace chr = std::chrono;

    namespace
    {
        constexpr size_t kBlock = 512;

        struct Entry
        {
            std::string file;  // "" for the folder or the .xcontent; else the file's name in the folder
            bool xcontent = false;
            bool dir = false;
            size_t offset = 0;  // of the body in the archive
            uint64_t size = 0;
            int64_t mtime = 0;
        };

        struct Parsed
        {
            Info info;
            std::vector<Entry> entries;
        };

        // An octal field (NUL- or space-terminated); false for anything else,
        // including base-256 sizes (no save needs them).
        bool Octal(const char* field, size_t width, uint64_t& out)
        {
            out = 0;
            size_t i = 0;
            while (i < width && field[i] == ' ')
                i++;
            bool digits = false;
            for (; i < width && field[i] >= '0' && field[i] <= '7'; i++)
            {
                if (out > (UINT64_MAX >> 3))
                    return false;
                out = out * 8 + uint64_t(field[i] - '0');
                digits = true;
            }
            for (; i < width; i++)
                if (field[i] != ' ' && field[i] != '\0')
                    return false;
            return digits;
        }

        std::string Field(const char* field, size_t width)
        {
            return std::string(field, strnlen(field, width));
        }

        bool ReadArchive(const fs::path& archive, std::string& bytes, std::string& error)
        {
            std::error_code ec;
            auto size = fs::file_size(archive, ec);
            if (ec)
            {
                error = "Can't read " + archive.filename().string() + ": " + ec.message();
                return false;
            }
            if (size > kMaxBytes)
            {
                error = archive.filename().string() + " is too large for a save (over 64 MB)";
                return false;
            }
            std::ifstream f(archive, std::ios::binary);
            bytes.assign(std::istreambuf_iterator<char>(f), {});
            if (!f && !f.eof())
            {
                error = "Can't read " + archive.filename().string();
                return false;
            }
            return true;
        }

        bool Parse(const std::string& bytes, Parsed& out, std::string& error)
        {
            out = {};
            std::set<std::string> seen;
            bool haveXcontent = false, haveDir = false, ended = false;
            uint64_t total = 0;
            size_t pos = 0;
            auto fail = [&](const std::string& why) { error = "Not a SpeedBreaker save: " + why; return false; };
            while (pos + kBlock <= bytes.size())
            {
                const char* h = bytes.data() + pos;
                if (std::all_of(h, h + kBlock, [](char c) { return c == 0; }))
                {
                    ended = true;
                    break;
                }
                if (std::memcmp(h + 257, "ustar", 5) != 0)
                    return fail("not a tar archive");
                uint64_t stored = 0;
                if (!Octal(h + 148, 8, stored))
                    return fail("a damaged header");
                uint64_t sum = 0;
                for (size_t i = 0; i < kBlock; i++)
                    sum += (i >= 148 && i < 156) ? uint8_t(' ') : uint8_t(h[i]);
                if (sum != stored)
                    return fail("a damaged header (checksum)");
                std::string name = Field(h, 100), prefix = Field(h + 345, 155);
                std::string path = prefix.empty() ? name : prefix + "/" + name;
                uint64_t size = 0, mtime = 0;
                if (!Octal(h + 124, 12, size))
                    return fail("a damaged size in " + path);
                Octal(h + 136, 12, mtime);
                const char type = h[156];
                const bool isFile = type == '0' || type == '\0', isDir = type == '5';
                if (!isFile && !isDir)
                    return fail(path + " is a link or a special file");
                if (size > kMaxBytes || (total += size) > kMaxBytes)
                    return fail(path + " is too large for a save");
                const size_t body = pos + kBlock;
                const uint64_t padded = (size + kBlock - 1) / kBlock * kBlock;
                if (padded > bytes.size() - body)
                    return fail("it ends early (truncated)");
                pos = body + size_t(padded);

                if (path.empty() || path[0] == '/')
                    return fail("an absolute path (" + path + ")");
                if (isDir && path.back() == '/')
                    path.pop_back();
                std::vector<std::string> parts;
                for (size_t start = 0;;)
                {
                    size_t slash = path.find('/', start);
                    parts.push_back(path.substr(start, slash == std::string::npos ? std::string::npos : slash - start));
                    if (slash == std::string::npos)
                        break;
                    start = slash + 1;
                }
                for (const std::string& part : parts)
                    if (part.empty() || part == "." || part == "..")
                        return fail("a path outside the save (" + path + ")");
                if (!saves::IsFolder(parts[0]))
                    return fail(parts[0] + " isn't a save folder");
                if (!seen.insert(path).second)
                    return fail(path + " is in it twice");

                Entry e{ "", false, isDir, body, size, int64_t(mtime) };
                std::string profile;
                if (parts.size() == 2 && isFile && parts[1].ends_with(".xcontent"))
                {
                    profile = parts[1].substr(0, parts[1].size() - 9);
                    e.xcontent = true;
                    if (size < saves::kXcontentSize)
                        return fail(path + " is too short for a save's header");
                    haveXcontent = true;
                }
                else if (parts.size() == 2 && isDir)
                {
                    profile = parts[1];
                    haveDir = true;
                }
                else if (parts.size() == 3 && isFile)
                {
                    profile = parts[1];
                    e.file = parts[2];
                    if (!saves::SafeName(e.file) && !(e.file.size() <= 255 && e.file[0] != '.' && e.file.find_first_of("\\:") == std::string::npos))
                        return fail(path + " has an unusable name");
                    haveDir = true;
                }
                else
                    return fail(path + " isn't part of a save");
                if (!saves::SafeName(profile))
                    return fail(path + " has an unusable name");
                if (out.info.name.empty())
                {
                    out.info.folder = parts[0];
                    out.info.name = profile;
                }
                else if (out.info.folder != parts[0] || out.info.name != profile)
                    return fail("it holds more than one save");
                out.info.bytes += size;
                out.entries.push_back(e);
            }
            if (!ended)
                return fail("it ends early (truncated)");
            if (out.entries.empty())
                return fail("it's empty");
            if (!haveXcontent)
                return fail("the save's header (" + out.info.name + ".xcontent) is missing");
            if (!haveDir)
                return fail("the save's folder (" + out.info.name + ") is missing");
            return true;
        }

        std::string Header(const std::string& path, bool dir, uint64_t size, int64_t mtime, std::string& error)
        {
            std::string h(kBlock, '\0');
            std::string name = path, prefix;
            if (name.size() > 100)
            {
                size_t slash = name.rfind('/', name.size() - 2);
                if (slash == std::string::npos || slash > 155 || name.size() - slash - 1 > 100)
                {
                    error = "the name " + path + " is too long";
                    return {};
                }
                prefix = name.substr(0, slash);
                name = name.substr(slash + 1);
            }
            std::memcpy(&h[0], name.data(), name.size());
            std::snprintf(&h[100], 8, "%07o", dir ? 0755u : 0644u);
            std::snprintf(&h[108], 8, "%07o", 0u);
            std::snprintf(&h[116], 8, "%07o", 0u);
            char field[13];
            std::snprintf(field, sizeof field, "%011llo", (unsigned long long)size);
            std::memcpy(&h[124], field, 11);
            std::snprintf(field, sizeof field, "%011llo", (unsigned long long)std::max<int64_t>(0, mtime));
            std::memcpy(&h[136], field, 11);
            h[156] = dir ? '5' : '0';
            std::memcpy(&h[257], "ustar", 6);
            std::memcpy(&h[263], "00", 2);
            std::memcpy(&h[345], prefix.data(), prefix.size());
            std::memset(&h[148], ' ', 8);
            unsigned sum = 0;
            for (unsigned char c : h)
                sum += c;
            std::snprintf(&h[148], 8, "%06o", sum);
            h[155] = ' ';
            return h;
        }

        int64_t UnixTime(const fs::path& p)
        {
            std::error_code ec;
            auto t = fs::last_write_time(p, ec);
            if (ec)
                return 0;
            return chr::duration_cast<chr::seconds>(chr::clock_cast<chr::system_clock>(t).time_since_epoch()).count();
        }

        void SetTime(const fs::path& p, int64_t seconds)
        {
            if (seconds <= 0)
                return;
            std::error_code ec;
            fs::last_write_time(p, chr::clock_cast<fs::file_time_type::clock>(chr::system_clock::time_point(chr::seconds(seconds))), ec);
        }

        bool WriteFile(const fs::path& p, std::string_view data)
        {
            std::ofstream f(p, std::ios::binary | std::ios::trunc);
            f.write(data.data(), std::streamsize(data.size()));
            f.close();
            return bool(f);
        }
    }

    bool Write(const fs::path& userDir, const saves::Profile& p, const fs::path& out, std::string& error)
    {
        if (!saves::IsFolder(p.folder) || !saves::SafeName(p.name))
        {
            error = "Not a save profile: " + p.folder + "/" + p.name;
            return false;
        }
        const fs::path folder = userDir / p.folder, dir = folder / p.name, xcontent = folder / (p.name + ".xcontent");
        std::error_code ec;
        std::string tar;
        auto add = [&](const std::string& path, const fs::path& source, bool isDir) {
            std::string body;
            if (!isDir)
            {
                std::ifstream f(source, std::ios::binary);
                if (!f)
                {
                    error = "Can't read " + source.string();
                    return false;
                }
                body.assign(std::istreambuf_iterator<char>(f), {});
            }
            std::string h = Header(path, isDir, body.size(), UnixTime(source), error);
            if (h.empty())
                return false;
            tar += h;
            tar += body;
            tar.resize((tar.size() + kBlock - 1) / kBlock * kBlock, '\0');
            if (tar.size() > kMaxBytes)
            {
                error = "The save is too large (over 64 MB)";
                return false;
            }
            return true;
        };
        if (!add(p.folder + "/" + p.name + ".xcontent", xcontent, false) || !add(p.folder + "/" + p.name + "/", dir, true))
            return false;
        std::vector<fs::path> files;
        for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        {
            std::error_code fec;
            if (it->is_regular_file(fec))
                files.push_back(it->path());
            else
            {
                error = "The save holds something that isn't a file: " + it->path().filename().string();
                return false;
            }
        }
        if (ec)
        {
            error = "Can't read " + dir.string() + ": " + ec.message();
            return false;
        }
        std::sort(files.begin(), files.end());
        for (const fs::path& f : files)
            if (!add(p.folder + "/" + p.name + "/" + f.filename().string(), f, false))
                return false;
        tar += std::string(2 * kBlock, '\0');

        fs::path partial = out;
        partial += ".partial";
        if (!WriteFile(partial, tar))
        {
            fs::remove(partial, ec);
            error = "Can't write " + out.string();
            return false;
        }
        fs::rename(partial, out, ec);
        if (ec)
        {
            fs::remove(partial, ec);
            error = "Can't write " + out.string() + ": " + ec.message();
            return false;
        }
        return true;
    }

    bool Inspect(const fs::path& archive, Info& info, std::string& error)
    {
        std::string bytes;
        Parsed parsed;
        if (!ReadArchive(archive, bytes, error) || !Parse(bytes, parsed, error))
            return false;
        info = parsed.info;
        return true;
    }

    bool Extract(const fs::path& archive, const fs::path& destUserDir, std::string& error)
    {
        std::string bytes;
        Parsed parsed;
        if (!ReadArchive(archive, bytes, error) || !Parse(bytes, parsed, error))
            return false;
        const Info& info = parsed.info;
        const fs::path folder = destUserDir / info.folder, dir = folder / info.name, xcontent = folder / (info.name + ".xcontent");
        std::error_code ec;
        if (fs::exists(fs::symlink_status(dir, ec)) || fs::exists(fs::symlink_status(xcontent, ec)))
        {
            error = "A save named " + info.name + " is already there";
            return false;
        }
        fs::create_directories(folder, ec);
        if (ec)
        {
            error = "Can't create " + folder.string() + ": " + ec.message();
            return false;
        }
        const fs::path staging = folder / (".import-" + std::to_string(getpid()) + "-" + info.name);
        fs::remove_all(staging, ec);
        auto cleanup = [&] { std::error_code c; fs::remove_all(staging, c); };
        fs::create_directories(staging / info.name, ec);
        if (ec)
        {
            error = "Can't write to " + folder.string() + ": " + ec.message();
            cleanup();
            return false;
        }
        int64_t dirTime = 0;
        for (const Entry& e : parsed.entries)
        {
            if (e.dir)
            {
                dirTime = e.mtime;
                continue;
            }
            fs::path to = e.xcontent ? staging / (info.name + ".xcontent") : staging / info.name / e.file;
            if (!WriteFile(to, std::string_view(bytes).substr(e.offset, size_t(e.size))))
            {
                error = "Can't write " + to.filename().string() + " (is the disk full?)";
                cleanup();
                return false;
            }
            SetTime(to, e.mtime);
        }
        SetTime(staging / info.name, dirTime);
        fs::rename(staging / info.name, dir, ec);
        if (ec)
        {
            error = "Can't put the save in place: " + ec.message();
            cleanup();
            return false;
        }
        fs::rename(staging / (info.name + ".xcontent"), xcontent, ec);
        if (ec)
        {
            error = "Can't put the save in place: " + ec.message();
            std::error_code c;
            fs::remove_all(dir, c);
            cleanup();
            return false;
        }
        cleanup();
        return true;
    }
}

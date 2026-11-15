// SpeedBreaker desktop launcher. GPL-3.0-or-later (see COPYING). See backups.h.
#include "backups.h"

#include <algorithm>
#include <ctime>

#include <unistd.h>

namespace launcher::backups
{
    namespace fs = std::filesystem;

    namespace
    {
        std::string Stamp(std::chrono::system_clock::time_point now)
        {
            std::time_t t = std::chrono::system_clock::to_time_t(now);
            std::tm local{};
            localtime_r(&t, &local);
            char text[32];
            std::strftime(text, sizeof text, "%Y%m%d-%H%M%S", &local);
            return text;
        }

        // The profile moved aside while its replacement is put in place.
        struct Aside
        {
            fs::path dir, xcontent, dirFrom, xcontentFrom;
            bool moved = false;
        };

        bool MoveAside(const fs::path& userDir, const sbsave::Info& info, Aside& a, std::string& error)
        {
            const fs::path folder = userDir / info.folder;
            const std::string tag = ".old-" + std::to_string(getpid()) + "-" + info.name;
            a.dirFrom = folder / info.name;
            a.xcontentFrom = folder / (info.name + ".xcontent");
            a.dir = folder / tag;
            a.xcontent = folder / (tag + ".xcontent");
            std::error_code ec;
            fs::rename(a.dirFrom, a.dir, ec);
            if (ec)
            {
                error = "Can't replace the save " + info.name + ": " + ec.message();
                return false;
            }
            fs::rename(a.xcontentFrom, a.xcontent, ec);
            if (ec)
            {
                std::error_code back;
                fs::rename(a.dir, a.dirFrom, back);
                error = "Can't replace the save " + info.name + ": " + ec.message();
                return false;
            }
            a.moved = true;
            return true;
        }

        void PutBack(const Aside& a)
        {
            std::error_code ec;
            fs::rename(a.dir, a.dirFrom, ec);
            fs::rename(a.xcontent, a.xcontentFrom, ec);
        }

        void Drop(const Aside& a)
        {
            std::error_code ec;
            fs::remove_all(a.dir, ec);
            fs::remove(a.xcontent, ec);
        }
    }

    fs::path Dir(const fs::path& userDir)
    {
        return userDir / "save-backups";
    }

    std::optional<fs::path> Backup(const fs::path& userDir, const saves::Profile& p, std::chrono::system_clock::time_point now,
        std::string& error)
    {
        const fs::path dir = Dir(userDir);
        std::error_code ec;
        fs::create_directories(dir, ec);
        if (ec)
        {
            error = "Can't create " + dir.string() + ": " + ec.message();
            return std::nullopt;
        }
        std::string base = p.name + (p.folder == "save" ? "" : "-" + p.folder) + "-" + Stamp(now);
        fs::path out = dir / (base + ".sbsave");
        for (int n = 2; fs::exists(fs::symlink_status(out, ec)); n++)
            out = dir / (base + "-" + std::to_string(n) + ".sbsave");
        if (!sbsave::Write(userDir, p, out, error))
            return std::nullopt;
        return out;
    }

    std::vector<Entry> List(const fs::path& userDir, const saves::Profile& p)
    {
        std::vector<Entry> out;
        std::error_code ec;
        for (fs::directory_iterator it(Dir(userDir), ec), end; !ec && it != end; it.increment(ec))
        {
            if (it->path().extension() != ".sbsave")
                continue;
            Entry e{ it->path(), {}, {} };
            std::string error;
            if (!sbsave::Inspect(e.path, e.info, error) || e.info.folder != p.folder || e.info.name != p.name)
                continue;
            std::error_code tec;
            e.when = it->last_write_time(tec);
            out.push_back(std::move(e));
        }
        std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) { return a.when > b.when; });
        return out;
    }

    bool Replace(const fs::path& userDir, const fs::path& archive, std::chrono::system_clock::time_point now, std::string& error)
    {
        sbsave::Info info;
        if (!sbsave::Inspect(archive, info, error))
            return false;
        const fs::path folder = userDir / info.folder;
        std::error_code ec;
        const bool there = fs::exists(fs::symlink_status(folder / info.name, ec)) ||
                           fs::exists(fs::symlink_status(folder / (info.name + ".xcontent"), ec));
        Aside aside;
        if (there)
        {
            if (!Backup(userDir, saves::Profile{ info.folder, info.name, "", {} }, now, error))
            {
                error = "The save " + info.name + " there couldn't be backed up, so it was left as it is: " + error;
                return false;
            }
            if (!MoveAside(userDir, info, aside, error))
                return false;
        }
        if (!sbsave::Extract(archive, userDir, error))
        {
            if (aside.moved)
                PutBack(aside);
            return false;
        }
        if (aside.moved)
            Drop(aside);
        return true;
    }

    bool Remove(const fs::path& userDir, const saves::Profile& p, std::chrono::system_clock::time_point now, std::string& error)
    {
        if (!Backup(userDir, p, now, error))
        {
            error = "The save couldn't be backed up, so it wasn't deleted: " + error;
            return false;
        }
        const fs::path folder = userDir / p.folder;
        std::error_code ec;
        fs::remove_all(folder / p.name, ec);
        if (ec)
        {
            error = "Can't delete " + p.name + ": " + ec.message();
            return false;
        }
        fs::remove(folder / (p.name + ".xcontent"), ec);
        if (ec)
        {
            error = "Can't delete " + p.name + ".xcontent: " + ec.message();
            return false;
        }
        return true;
    }
}

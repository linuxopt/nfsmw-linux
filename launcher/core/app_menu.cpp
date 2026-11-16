// SpeedBreaker desktop launcher. GPL-3.0-or-later (see COPYING). See app_menu.h.
#include "app_menu.h"
#include "durable.h"

#include <fstream>
#include <iterator>
#include <system_error>

namespace launcher::app_menu
{
    namespace fs = std::filesystem;

    namespace
    {
        constexpr const char* kMark = "X-SpeedBreaker-Menu=1";

        bool ReadFile(const fs::path& p, std::string& out)
        {
            std::ifstream f(p, std::ios::binary);
            if (!f)
                return false;
            out.assign(std::istreambuf_iterator<char>(f), {});
            return true;
        }

        // Writes `bytes` to `path` through a .partial file, flushed, unless
        // it already holds them.
        bool Replace(const fs::path& path, std::string_view bytes, bool& changed, std::string& error)
        {
            std::string now;
            if (ReadFile(path, now) && now == bytes)
                return true;
            std::error_code ec;
            fs::create_directories(path.parent_path(), ec);
            fs::path partial = path;
            partial += ".partial";
            {
                std::ofstream f(partial, std::ios::binary | std::ios::trunc);
                f.write(bytes.data(), std::streamsize(bytes.size()));
                f.close();
                if (!f)
                {
                    fs::remove(partial, ec);
                    error = "can't write " + path.string();
                    return false;
                }
            }
            durable::SyncFile(partial);
            fs::rename(partial, path, ec);
            if (ec)
            {
                fs::remove(partial, ec);
                error = "can't write " + path.string();
                return false;
            }
            durable::SyncDir(path.parent_path());
            changed = true;
            return true;
        }

        std::string EntryText(const std::string& appImage, const fs::path& icon)
        {
            return "[Desktop Entry]\n"
                   "Type=Application\n"
                   "Name=Need for Speed: Most Wanted\n"
                   "GenericName=Racing game\n"
                   "Comment=Need for Speed: Most Wanted (2005) in SpeedBreaker\n"
                   "Exec=" + ExecLine(appImage) + "\n"
                   "Icon=" + icon.string() + "\n"
                   "Terminal=false\n"
                   "Categories=Game;\n"
                   "Keywords=NFS;Most Wanted;SpeedBreaker;racing;\n"
                   "# Written by SpeedBreaker's launcher (Add to App Menu); it removes it again.\n"
                   + std::string(kMark) + "\n";
        }
    }

    std::optional<fs::path> DataHome(const char* xdgDataHome, const char* home)
    {
        if (xdgDataHome && xdgDataHome[0] == '/')
            return fs::path(xdgDataHome);
        if (home && home[0] == '/')
            return fs::path(home) / ".local" / "share";
        return std::nullopt;
    }

    Paths PathsIn(const fs::path& dataHome)
    {
        return { dataHome / "applications" / "speedbreaker.desktop", dataHome / "icons" / "speedbreaker-nfsmw.png" };
    }

    State Check(const Paths& paths)
    {
        std::string text;
        if (!ReadFile(paths.entry, text))
        {
            std::error_code ec;
            return fs::exists(paths.entry, ec) ? State::Other : State::None;
        }
        return text.find("\n" + std::string(kMark) + "\n") != std::string::npos ? State::Ours : State::Other;
    }

    std::string ExecLine(const std::string& appImage)
    {
        // Inside the quotes, " ` $ \ take a backslash; then the value, a
        // string, has its own backslashes doubled; % starts a field code.
        std::string out = "\"";
        for (char c : appImage)
        {
            if (c == '"' || c == '`' || c == '$')
                out += '\\', out += c;
            else if (c == '\\')
                out += "\\\\\\\\";
            else if (c == '%')
                out += "%%";
            else
                out += c;
        }
        return out + "\"";
    }

    bool Write(const Paths& paths, const std::string& appImage, std::span<const uint8_t> iconPng, std::string& error,
        bool* changed)
    {
        bool any = false;
        if (changed)
            *changed = false;
        if (Check(paths) == State::Other)
        {
            error = paths.entry.string() + " is already there and isn't SpeedBreaker's: it was left as it is.";
            return false;
        }
        if (appImage.find_first_of("\r\n") != std::string::npos || paths.icon.string().find_first_of("\r\n") != std::string::npos)
        {
            error = "The AppImage's path has a line break, which an app-menu entry can't hold.";
            return false;
        }
        // The icon first, so the entry never names a missing one.
        if (!Replace(paths.icon, std::string_view(reinterpret_cast<const char*>(iconPng.data()), iconPng.size()), any, error) ||
            !Replace(paths.entry, EntryText(appImage, paths.icon), any, error))
            return false;
        if (changed)
            *changed = any;
        return true;
    }

    bool Remove(const Paths& paths, std::string& error)
    {
        const State state = Check(paths);
        if (state == State::Other)
        {
            error = paths.entry.string() + " isn't SpeedBreaker's: it was left as it is.";
            return false;
        }
        std::error_code ec;
        if (state == State::Ours && !fs::remove(paths.entry, ec) && ec)
        {
            error = "can't remove " + paths.entry.string();
            return false;
        }
        fs::remove(paths.icon, ec);
        if (ec)
        {
            error = "can't remove " + paths.icon.string();
            return false;
        }
        return true;
    }
}

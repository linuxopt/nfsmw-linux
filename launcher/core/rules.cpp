// SpeedBreaker desktop launcher. GPL-3.0-or-later (see COPYING). See rules.h.
#include "rules.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

#include <video/steam_device.h>

namespace launcher::rules
{
    namespace fs = std::filesystem;

    namespace
    {
        std::string ReadLine(const fs::path& path)
        {
            std::ifstream f(path);
            std::string line;
            std::getline(f, line);
            while (!line.empty() && (line.back() == ' ' || line.back() == '\r' || line.back() == '\t'))
                line.pop_back();
            return line;
        }
    }

    bool SmoothMotionShown(std::span<const double> screenHz, bool steamDeck, bool optionOn)
    {
        if (optionOn)
            return true;
        if (steamDeck)
            return false;
        for (double hz : screenHz)
            if (std::fabs(hz - 90.0) <= 1.5 || std::fabs(hz - 120.0) <= 1.5)
                return true;
        return false;
    }

    bool SteamDeck(const fs::path& dmiDir)
    {
        if (const char* v = std::getenv("NFSMW_HFR_DECK"); v && v[0] == '1')
            return false;
        return video::IsSteamDeckDmi(ReadLine(dmiDir / "sys_vendor"), ReadLine(dmiDir / "product_name"));
    }

    bool HdTexturesShown(const fs::path& userDir)
    {
        std::error_code ec;
        return fs::is_regular_file(userDir / "texture-pack" / "pack.json", ec);
    }

    bool GameRunning(const fs::path& procDir)
    {
        std::error_code ec;
        for (fs::directory_iterator it(procDir, ec), end; !ec && it != end; it.increment(ec))
        {
            const std::string pid = it->path().filename().string();
            if (pid.empty() || pid.find_first_not_of("0123456789") != std::string::npos)
                continue;
            std::ifstream f(it->path() / "cmdline", std::ios::binary);
            const std::string cmdline(std::istreambuf_iterator<char>(f), {});
            size_t start = 0;
            while (start < cmdline.size())
            {
                size_t end = cmdline.find('\0', start);
                if (end == std::string::npos)
                    end = cmdline.size();
                std::string_view arg(cmdline.data() + start, end - start);
                if (arg.ends_with("/lib/SpeedBreaker"))
                    return true;
                start = end + 1;
            }
        }
        return false;
    }

    bool GameMode(const char* gameModeEnv, const char* gamescopeWayland, const char* xdgDesktop)
    {
        if (gameModeEnv)
            return gameModeEnv[0] == '1';
        return (gamescopeWayland && *gamescopeWayland) || (xdgDesktop && std::strcmp(xdgDesktop, "gamescope") == 0);
    }
}

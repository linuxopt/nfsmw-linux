// SpeedBreaker desktop launcher. GPL-3.0-or-later (see COPYING). See release.h.
#include "release.h"

#include <fstream>
#include <optional>
#include <string>

#include <game/language.h>
#include <install/manifest.h>

namespace launcher::release
{
    namespace fs = std::filesystem;

    namespace
    {
        const install::Manifest* At(const fs::path& dir)
        {
            std::error_code ec;
            const uint64_t size = fs::file_size(dir / "default.xex", ec);
            return ec ? nullptr : install::ManifestForXexSize(size);
        }

        // The installer's record of a folder it installed to (an absolute path).
        std::optional<fs::path> Recorded(const fs::path& userDir)
        {
            std::ifstream f(userDir / "game-location");
            std::string line;
            if (!std::getline(f, line))
                return std::nullopt;
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
                line.pop_back();
            while (line.size() > 1 && line.back() == '/')
                line.pop_back();
            if (line.empty() || line[0] != '/')
                return std::nullopt;
            return fs::path(line);
        }
    }

    std::optional<fs::path> InstalledGameDir(const fs::path& userDir, const char* gameDirEnv)
    {
        if (gameDirEnv && *gameDirEnv && At(gameDirEnv))
            return fs::path(gameDirEnv);
        if (std::optional<fs::path> recorded = Recorded(userDir); recorded && At(*recorded))
            return recorded;
        if (At(userDir / "game"))
            return userDir / "game";
        return std::nullopt;
    }

    std::string_view InstalledRegion(const fs::path& userDir, const char* gameDirEnv)
    {
        const std::optional<fs::path> dir = InstalledGameDir(userDir, gameDirEnv);
        return dir ? At(*dir)->region : std::string_view();
    }

    int32_t DiscLanguageDefault(const fs::path& userDir, const char* gameDirEnv)
    {
        const std::string_view region = InstalledRegion(userDir, gameDirEnv);
        return region.empty() ? -1 : game::language::DiscLanguage(region);
    }
}

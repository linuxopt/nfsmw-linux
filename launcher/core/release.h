// SpeedBreaker desktop launcher. GPL-3.0-or-later (see COPYING).
//
// The installed disc's release, found as the game finds it before loading
// its settings (runtime/main.cpp SetReleaseDefaults, install/locate.h
// FindGameInstall): default.xex's size, in NFSMW_GAME_DIR, else the folder
// the installer recorded (<data>/game-location), else <data>/game. The
// launcher needs it for one thing: a PAL disc's own language (Spanish,
// German) is the Language row's default, which the launcher must show and
// compare against as the game does. Cheap: a few stat calls, no hashing.
#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

namespace launcher::release
{
    // "usa", "pal-es", "pal-de" (install/manifest.h Manifest::region), or ""
    // when no installed release is found. `gameDirEnv`: NFSMW_GAME_DIR's value.
    // The folder holding the installed game's default.xex (a supported
    // release's, by its size), looked for as above; nullopt when none.
    std::optional<std::filesystem::path> InstalledGameDir(const std::filesystem::path& userDir, const char* gameDirEnv);
    std::string_view InstalledRegion(const std::filesystem::path& userDir, const char* gameDirEnv);
    // That release's disc language (game/language.h DiscLanguage), or -1.
    int32_t DiscLanguageDefault(const std::filesystem::path& userDir, const char* gameDirEnv);
}

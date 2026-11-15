// SpeedBreaker desktop launcher. GPL-3.0-or-later (see COPYING).
//
// The player's save profiles, as the game keeps them (kernel/xam.cpp
// ContentRoot, ScanSavedContent): a <name>/ folder with the game's files,
// and <name>.xcontent beside it (the profile's XCONTENT_DATA: DeviceID,
// type 1 for a save, the display name in UTF-16BE, the file name). Each
// release has its own folder in the data folder (user/paths.h GetSavePath):
// save/ (USA), save-pal-es/, save-pal-de/. The launcher lists them all,
// since it doesn't work out which release is installed.
#pragma once
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace launcher::saves
{
    inline constexpr const char* kFolders[] = { "save", "save-pal-es", "save-pal-de" };
    inline constexpr size_t kXcontentSize = 306;  // XCONTENT_DATA without its tail padding (the file has 308)

    bool IsFolder(std::string_view folder);
    const char* ReleaseLabel(std::string_view folder);  // "USA", "Europe (Spanish)", "Europe (German)"

    // A name the game could have made and a path can hold safely: not empty,
    // no '/', '\\' or ':', not starting with '.', at most 42 bytes
    // (XCONTENT_MAX_FILENAME).
    bool SafeName(std::string_view name);

    struct Profile
    {
        std::string folder;       // "save", "save-pal-es", "save-pal-de"
        std::string name;         // the folder's and the .xcontent's name
        std::string displayName;  // the .xcontent's, or "" without one
        std::filesystem::file_time_type lastPlayed{};
    };

    // Every profile under `userDir`, release folders in kFolders order, then
    // by name. A profile is a <name>.xcontent of at least kXcontentSize bytes
    // with type 1 and a <name>/ folder; last played is its newest file's
    // time (the folder's own when it has none).
    std::vector<Profile> List(const std::filesystem::path& userDir);

    // The display name in an .xcontent (UTF-16BE at offset 8, up to 128
    // units or a 0), as UTF-8; "" when there's none.
    std::string DisplayName(std::span<const uint8_t> xcontent);
}

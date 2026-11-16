// SpeedBreaker desktop launcher. GPL-3.0-or-later (see COPYING).
//
// An entry for the game in the desktop's app menu, for the AppImage, which
// a menu doesn't otherwise know about: the launcher's "Add to App Menu"
// writes <data home>/applications/speedbreaker.desktop (Exec: the AppImage,
// Icon: the game's own icon, install/title_icon.h) and the icon beside it
// in <data home>/icons. The file name is the launcher's desktop file name
// (main.cpp), so desktops that match windows to entries by it (Wayland) find
// it. The entry is marked as SpeedBreaker's (X-SpeedBreaker-Menu=1): a
// speedbreaker.desktop without the mark is someone else's, and is never
// written over or removed.
#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>

namespace launcher::app_menu
{
    // $XDG_DATA_HOME when absolute, else $HOME/.local/share; nullopt
    // without an absolute home.
    std::optional<std::filesystem::path> DataHome(const char* xdgDataHome, const char* home);

    struct Paths
    {
        std::filesystem::path entry;  // <data home>/applications/speedbreaker.desktop
        std::filesystem::path icon;   // <data home>/icons/speedbreaker-nfsmw.png
    };
    Paths PathsIn(const std::filesystem::path& dataHome);

    enum class State
    {
        None,   // no entry
        Ours,   // SpeedBreaker's
        Other,  // a speedbreaker.desktop someone else wrote
    };
    State Check(const Paths& paths);

    // The Exec value that runs `appImage`: quoted, with the Desktop Entry
    // spec's reserved characters escaped and % doubled.
    std::string ExecLine(const std::string& appImage);

    // Adds the entry, or brings ours up to date (another AppImage path, a
    // new icon), writing only the files whose contents change; `changed`
    // says whether any did. False (and `error`) for someone else's entry, a
    // path a desktop entry can't hold (a newline), or a failed write.
    bool Write(const Paths& paths, const std::string& appImage, std::span<const uint8_t> iconPng, std::string& error,
        bool* changed = nullptr);

    // Removes our entry and its icon (nothing there is fine); false (and
    // `error`) for someone else's entry or a failed removal.
    bool Remove(const Paths& paths, std::string& error);
}

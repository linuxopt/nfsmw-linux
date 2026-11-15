// SpeedBreaker desktop launcher. GPL-3.0-or-later (see COPYING).
//
// What the launcher shows, by the rules the game's own menu follows
// (ui/settings_menu.cpp Shown) as far as a program without the game's
// renderer and presenter can see them, and the facts it checks first.
#pragma once
#include <filesystem>
#include <span>

namespace launcher::rules
{
    // Smooth Motion: the game offers it on Linux at about 90 Hz, or at twice
    // 60 Hz (video/refresh_multiple.h SmoothMotionRate), never on a Steam
    // Deck (video/presenter.cpp HfrOffered). While it's on it is always
    // shown, so it can be turned off. `screenHz`: each screen's rate.
    bool SmoothMotionShown(std::span<const double> screenHz, bool steamDeck, bool optionOn);
    // A Steam Deck from its DMI names in `dmiDir` (/sys/class/dmi/id):
    // video/steam_device.h IsSteamDeckDmi; NFSMW_HFR_DECK=1 counts as not one
    // (the game offers Smooth Motion there anyway then).
    bool SteamDeck(const std::filesystem::path& dmiDir);
    // HD Textures: a texture pack is installed (texture-pack/pack.json).
    bool HdTexturesShown(const std::filesystem::path& userDir);
    // The game is running: a process (`procDir`, /proc) whose command line
    // has an argument ending in /lib/SpeedBreaker (the bundle's binary, run
    // directly or through a loader).
    bool GameRunning(const std::filesystem::path& procDir);
    // SteamOS Game Mode (video/presenter.cpp DetectGameMode): NFSMW_GAME_MODE
    // decides when set, else gamescope's display or desktop name.
    bool GameMode(const char* gameModeEnv, const char* gamescopeWayland, const char* xdgDesktop);
}

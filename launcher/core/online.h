// SpeedBreaker desktop launcher. GPL-3.0-or-later (see COPYING).
//
// Online play's settings, online.toml in the data folder, as the game reads
// it (runtime/net/online.h): on or off, the player's name, the server, and
// the id the game made (kept as it is).
#pragma once
#include <filesystem>
#include <string>

namespace launcher::online
{
    // The server the game uses when online.toml names none.
    constexpr const char* kDefaultServer = "62.3.24.62:39300";

    struct Config
    {
        bool enabled = false;
        std::string name;
        std::string server = kDefaultServer;
        std::string id;     // made by the game on first use; "" until then
    };

    // No file: off, default server (true). A file that doesn't parse: false
    // and `error` (the caller shouldn't overwrite it unasked). A file
    // without `enabled` (written by hand) is on.
    bool Load(const std::filesystem::path& userDir, Config& out, std::string& error);
    // Written whole (a .partial file, flushed, renamed over it).
    bool Save(const std::filesystem::path& userDir, const Config& config, std::string& error);

    // Why a name or server can't be used, or "" when it can. Names: 1-15
    // printable ASCII characters, no space at either end (what the game
    // and the server accept). Servers: host:port.
    std::string NameProblem(const std::string& name);
    std::string ServerProblem(const std::string& server);
}

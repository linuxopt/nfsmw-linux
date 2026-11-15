// SpeedBreaker desktop launcher. GPL-3.0-or-later (see COPYING).
//
// Save backups: one .sbsave (sbsave.h) each in save-backups/ in the data
// folder. Nothing the launcher does to a profile loses it: restoring or
// importing over a profile backs that profile up first (Replace), deleting
// one backs it up first (Remove), and a replace that fails part way leaves
// the old profile as it was.
#pragma once
#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "saves.h"
#include "sbsave.h"

namespace launcher::backups
{
    // userDir / "save-backups"
    std::filesystem::path Dir(const std::filesystem::path& userDir);

    // `p` to Dir()/<name>-YYYYMMDD-HHMMSS.sbsave (local time `now`), or
    // <name>-<folder>-... for a release other than USA; -2, -3... when the
    // name is taken. The path written, or nothing (`error` says why).
    std::optional<std::filesystem::path> Backup(const std::filesystem::path& userDir, const saves::Profile& p,
        std::chrono::system_clock::time_point now, std::string& error);

    struct Entry
    {
        std::filesystem::path path;
        sbsave::Info info;
        std::filesystem::file_time_type when{};
    };

    // `p`'s backups, by what each holds (not its file name), newest first.
    std::vector<Entry> List(const std::filesystem::path& userDir, const saves::Profile& p);

    // Restore or import `archive`: if its profile is there, back it up
    // (failure: stop) and move it aside, extract the archive, then drop the
    // old one; if the extract fails, the old one goes back.
    bool Replace(const std::filesystem::path& userDir, const std::filesystem::path& archive,
        std::chrono::system_clock::time_point now, std::string& error);

    // Back `p` up, then delete it.
    bool Remove(const std::filesystem::path& userDir, const saves::Profile& p, std::chrono::system_clock::time_point now,
        std::string& error);
}

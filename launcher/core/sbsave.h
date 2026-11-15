// SpeedBreaker desktop launcher. GPL-3.0-or-later (see COPYING).
//
// A .sbsave: one save profile in one file, to keep as a backup or carry to
// another machine. It is a plain ustar tar (any tar program opens it) of
//   <folder>/<name>.xcontent, <folder>/<name>/, <folder>/<name>/<file>...
// so it records its release folder (saves.h kFolders), and goes back to the
// same one. Written and read here, without a tar library: reading checks the
// whole archive first and refuses anything that isn't exactly one profile in
// one of the save folders (an absolute path, "..", a link or device, another
// folder or a second profile, a bad checksum, a truncated or oversized
// archive), so an archive from anywhere can only ever write a profile.
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>

#include "saves.h"

namespace launcher::sbsave
{
    inline constexpr uint64_t kMaxBytes = 64ull << 20;  // saves are kilobytes

    // `p` from `userDir` to `out`, through `out`.partial and a rename. Only
    // regular files directly in the profile's folder (the game writes no
    // subfolders).
    bool Write(const std::filesystem::path& userDir, const saves::Profile& p, const std::filesystem::path& out, std::string& error);

    struct Info
    {
        std::string folder;
        std::string name;
        uint64_t bytes = 0;  // the files' total size
    };

    // Reads and checks the whole archive; writes nothing.
    bool Inspect(const std::filesystem::path& archive, Info& info, std::string& error);

    // Inspect, then the profile into `destUserDir`/<folder>/, through a
    // staging folder there and renames. Never replaces a profile: fails when
    // <name>/ or <name>.xcontent is already there (backups.h's Replace moves
    // the old one aside first).
    bool Extract(const std::filesystem::path& archive, const std::filesystem::path& destUserDir, std::string& error);
}

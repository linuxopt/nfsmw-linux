// SpeedBreaker desktop launcher. GPL-3.0-or-later (see COPYING).
//
// Flushing to disk (fsync) what a save operation wrote, before anything is
// removed: a backup and its folder entry before a delete, an imported save
// and its folder before the old one goes. Without it a power cut in the
// next seconds could keep the deletion and lose the new files (ext4 and
// others commit a rename before the data of a file new under that name).
// The observer lets the tests see what was flushed, and when.
#pragma once
#include <filesystem>
#include <functional>

namespace launcher::durable
{
    // fsync a file / a folder (its entries). False when it can't be opened or
    // flushed.
    bool SyncFile(const std::filesystem::path& file);
    bool SyncDir(const std::filesystem::path& dir);

    // Called with each path flushed (tests); nullptr: none.
    void SetObserver(std::function<void(const std::filesystem::path&)> observer);
}

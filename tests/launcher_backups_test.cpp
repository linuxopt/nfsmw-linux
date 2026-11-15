// The desktop launcher's save backups (launcher/core/backups.h): one .sbsave
// per backup in save-backups/, restore and import through Replace (the
// profile there is backed up first, and kept whole when anything fails),
// delete through Remove (backed up first). Argument: a scratch folder.
#include <backups.h>
#include <durable.h>

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <unistd.h>

#include "launcher_fixture.h"

static int g_failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } } while (0)

namespace fs = std::filesystem;
using namespace launcher;

namespace
{
    std::chrono::system_clock::time_point At(int y, int mo, int d, int h, int mi, int s)
    {
        std::tm t{};
        t.tm_year = y - 1900;
        t.tm_mon = mo - 1;
        t.tm_mday = d;
        t.tm_hour = h;
        t.tm_min = mi;
        t.tm_sec = s;
        return std::chrono::system_clock::from_time_t(timegm(&t));
    }

    size_t Count(const fs::path& dir)
    {
        size_t n = 0;
        std::error_code ec;
        for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
            n++;
        return n;
    }
}

int main(int argc, char** argv)
{
    if (argc < 2)
        return 2;
    setenv("TZ", "UTC", 1);
    tzset();
    fs::path scratch = argv[1];
    fs::remove_all(scratch);
    fs::path user = scratch / "user";
    fixture::Profile(user, "save", "TJ", "career-1");
    fixture::Profile(user, "save", "TJ2", "other");
    fixture::Profile(user, "save-pal-de", "Ana", "karriere");
    const saves::Profile tj{ "save", "TJ", "TJ", {} }, tj2{ "save", "TJ2", "TJ2", {} }, ana{ "save-pal-de", "Ana", "Ana", {} };
    const auto now = At(2026, 11, 15, 9, 30, 0);
    std::string error;

    // Names.
    auto b1 = backups::Backup(user, tj, now, error);
    CHECK(b1 && b1->filename() == "TJ-20261115-093000.sbsave" && b1->parent_path() == backups::Dir(user), "named by time: %s",
        b1 ? b1->string().c_str() : error.c_str());
    auto b2 = backups::Backup(user, tj, now, error);
    CHECK(b2 && b2->filename() == "TJ-20261115-093000-2.sbsave", "the same second: -2");
    auto b3 = backups::Backup(user, ana, now, error);
    CHECK(b3 && b3->filename() == "Ana-save-pal-de-20261115-093000.sbsave", "another release's folder is in the name");
    auto b4 = backups::Backup(user, tj2, now, error);
    CHECK(b4.has_value(), "TJ2's");
    // A TJ2 backup named like a TJ one: matched by what's in it, not its name.
    fs::copy_file(*b4, backups::Dir(user) / "TJ-20200101-000000.sbsave");

    std::vector<backups::Entry> list = backups::List(user, tj);
    CHECK(list.size() == 2, "TJ's two backups only (%zu)", list.size());
    for (const auto& e : list)
        CHECK(e.info.name == "TJ" && e.info.folder == "save", "only TJ's");
    CHECK(backups::List(user, ana).size() == 1, "Ana's one");

    // Restore: the profile now is backed up, then replaced by the backup's.
    fixture::WriteBytes(user / "save" / "TJ" / "TJ", "career-2");
    size_t before = Count(backups::Dir(user));
    CHECK(backups::Replace(user, *b1, At(2026, 11, 15, 10, 0, 0), error), "restored: %s", error.c_str());
    CHECK(fixture::ReadBytes(user / "save" / "TJ" / "TJ") == "career-1", "the backup's save is back");
    CHECK(Count(backups::Dir(user)) == before + 1, "and the one it replaced was backed up first");
    CHECK(fs::exists(backups::Dir(user) / "TJ-20261115-100000.sbsave"), "as TJ-20261115-100000");
    CHECK(Count(user / "save") == 4, "no staging or old folder left (%zu)", Count(user / "save"));

    // Import of a profile that isn't there: no backup needed.
    fs::path other = scratch / "other";
    CHECK(backups::Replace(other, *b3, now, error), "imported: %s", error.c_str());
    CHECK(fixture::ReadBytes(other / "save-pal-de" / "Ana" / "Ana") == "karriere", "into its own release's folder");
    CHECK(!fs::exists(backups::Dir(other)), "nothing to back up");

    // A refused archive: no backup, the profile untouched.
    fixture::WriteBytes(scratch / "bad.sbsave", std::string(2048, 'z'));
    before = Count(backups::Dir(user));
    CHECK(!backups::Replace(user, scratch / "bad.sbsave", now, error) && !error.empty(), "refused");
    CHECK(Count(backups::Dir(user)) == before && fixture::ReadBytes(user / "save" / "TJ" / "TJ") == "career-1", "nothing changed");

    // A folder that can't be written: the profile stays whole.
    if (geteuid() != 0)
    {
        fs::permissions(user / "save", fs::perms::owner_write, fs::perm_options::remove);
        CHECK(!backups::Replace(user, *b2, At(2026, 11, 15, 11, 0, 0), error) && !error.empty(), "can't replace in a read-only folder");
        fs::permissions(user / "save", fs::perms::owner_write, fs::perm_options::add);
        CHECK(fixture::ReadBytes(user / "save" / "TJ" / "TJ") == "career-1" && fs::exists(user / "save" / "TJ.xcontent"), "TJ intact");
        CHECK(Count(user / "save") == 4, "nothing left behind (%zu)", Count(user / "save"));
    }
    else
        printf("  (running as root: the read-only folder case is skipped)\n");

    // Delete: backed up, then gone; the backup brings it back.
    CHECK(backups::Remove(user, tj2, At(2026, 11, 15, 12, 0, 0), error), "deleted: %s", error.c_str());
    CHECK(!fs::exists(user / "save" / "TJ2") && !fs::exists(user / "save" / "TJ2.xcontent"), "gone");
    fs::path kept = backups::Dir(user) / "TJ2-20261115-120000.sbsave";
    CHECK(fs::exists(kept), "kept as a backup");
    CHECK(backups::Replace(user, kept, now, error) && fixture::ReadBytes(user / "save" / "TJ2" / "TJ2") == "other", "and restored");

    // Flushed to disk before anything is removed: a power cut right after a
    // delete or a replace must never lose the save and its backup together.
    {
        fs::path dur = scratch / "durable";
        fixture::Profile(dur, "save", "TJ", "career-1");
        const saves::Profile p{ "save", "TJ", "TJ", {} };
        std::vector<std::string> synced;
        bool profileThereAtBackupSync = false, oldThereAtFolderSync = false;
        durable::SetObserver([&](const fs::path& path) {
            synced.push_back(path.string());
            if (path.extension() == ".sbsave")
                profileThereAtBackupSync = fs::exists(dur / "save" / "TJ");
        });
        CHECK(backups::Remove(dur, p, At(2026, 11, 16, 8, 0, 0), error), "deleted: %s", error.c_str());
        fs::path kept = backups::Dir(dur) / "TJ-20261116-080000.sbsave";
        auto has = [&](const fs::path& want) { return std::find(synced.begin(), synced.end(), want.string()) != synced.end(); };
        CHECK(has(kept) && has(backups::Dir(dur)), "the backup and its folder were flushed");
        CHECK(profileThereAtBackupSync, "before the profile was deleted");

        synced.clear();
        durable::SetObserver([&](const fs::path& path) {
            synced.push_back(path.string());
            if (path == dur / "save")
                oldThereAtFolderSync = oldThereAtFolderSync || !fs::exists(dur / "save" / "TJ" / "TJ") ||
                                        fixture::ReadBytes(dur / "save" / "TJ" / "TJ") == "career-1";
        });
        fixture::Profile(dur, "save", "TJ", "career-2");  // a new TJ, replaced by the backup
        CHECK(backups::Replace(dur, kept, At(2026, 11, 16, 9, 0, 0), error), "restored: %s", error.c_str());
        CHECK(has(dur / "save" / "TJ" / "TJ") || std::any_of(synced.begin(), synced.end(), [](const std::string& s) {
                  return s.find(".import-") != std::string::npos && s.ends_with("/TJ/TJ"); }),
            "the restored file was flushed");
        CHECK(has(dur / "save"), "and the save folder after the renames");
        size_t extracted = 0;
        for (const std::string& s : synced)
            extracted += s.ends_with(".xcontent");
        CHECK(extracted >= 1, "its header too");
        durable::SetObserver(nullptr);
    }

    if (g_failures)
        printf("launcher_backups_test: %d failure(s)\n", g_failures);
    else
        printf("launcher_backups_test: all passed\n");
    return g_failures ? 1 : 0;
}

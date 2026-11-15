// The installed disc's release as the desktop launcher finds it
// (launcher/core/release.h), the way the game does before its settings load
// (runtime/main.cpp SetReleaseDefaults): default.xex's size in NFSMW_GAME_DIR,
// the recorded install folder (game-location), or the data folder's game/.
// A PAL disc's language is the Language row's default.
// Argument: a scratch folder.
#include <release.h>

#include <cstdio>
#include <filesystem>
#include <fstream>

#include <game/language.h>
#include <install/manifest.h>

static int g_failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } } while (0)

namespace fs = std::filesystem;
using namespace launcher;

namespace
{
    const install::Manifest& Region(std::string_view region)
    {
        for (const install::Manifest* m : install::GameManifests())
            if (m->region == region)
                return *m;
        return install::GameManifest();
    }

    // A sparse default.xex of that release's size.
    void Xex(const fs::path& dir, std::string_view region)
    {
        fs::create_directories(dir);
        std::ofstream(dir / "default.xex", std::ios::binary).close();
        fs::resize_file(dir / "default.xex", Region(region).Xex().size);
    }
}

int main(int argc, char** argv)
{
    if (argc < 2)
        return 2;
    fs::remove_all(argv[1]);
    fs::create_directories(argv[1]);
    const fs::path scratch = fs::absolute(argv[1]);  // the installer records absolute paths
    fs::path user = scratch / "user";
    fs::create_directories(user);

    CHECK(release::InstalledRegion(user, nullptr).empty(), "nothing installed");
    CHECK(release::DiscLanguageDefault(user, nullptr) < 0, "no default");

    for (const install::Manifest* m : install::GameManifests())
    {
        fs::path u = scratch / ("u-" + std::string(m->region));
        Xex(u / "game", m->region);
        CHECK(release::InstalledRegion(u, nullptr) == m->region, "%s found in game/", std::string(m->region).c_str());
        CHECK(release::DiscLanguageDefault(u, nullptr) == game::language::DiscLanguage(m->region), "%s's language",
            std::string(m->region).c_str());
    }
    CHECK(release::DiscLanguageDefault(scratch / "u-usa", nullptr) < 0, "the USA disc: English, the table's own default");
    CHECK(release::DiscLanguageDefault(scratch / "u-pal-es", nullptr) >= 0, "a PAL disc has its own");

    // The recorded install folder before the data folder's game/, and
    // NFSMW_GAME_DIR before both. (Only USA and PAL ES here: PAL DE is in
    // builds that link the German code, the game's and the launcher's alike.)
    Xex(user / "game", "pal-es");
    Xex(scratch / "sdcard" / "nfs", "usa");
    std::ofstream(user / "game-location") << (scratch / "sdcard" / "nfs").string() << "/\n";
    CHECK(release::InstalledRegion(user, nullptr) == "usa", "the recorded folder");
    Xex(scratch / "envdir", "pal-es");
    CHECK(release::InstalledRegion(user, (scratch / "envdir").c_str()) == "pal-es", "NFSMW_GAME_DIR");
    // A folder without a known default.xex is passed over.
    fs::create_directories(scratch / "empty");
    CHECK(release::InstalledRegion(user, (scratch / "empty").c_str()) == "usa", "an unusable NFSMW_GAME_DIR is skipped");
    std::ofstream(scratch / "sdcard" / "nfs" / "default.xex", std::ios::trunc) << "x";
    CHECK(release::InstalledRegion(user, nullptr) == "pal-es", "an unknown default.xex is skipped");

    if (g_failures)
        printf("launcher_release_test: %d failure(s)\n", g_failures);
    else
        printf("launcher_release_test: all passed\n");
    return g_failures ? 1 : 0;
}

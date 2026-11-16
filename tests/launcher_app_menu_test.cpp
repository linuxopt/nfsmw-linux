// The launcher's app-menu entry (launcher/core/app_menu.h): where it goes,
// what it says, Exec quoting, adding, refreshing for a moved AppImage,
// removing, and never touching an entry that isn't SpeedBreaker's.
// Argument: a scratch folder.
#include <app_menu.h>

#include "launcher_fixture.h"

#include <cstring>

static int g_failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } } while (0)

namespace fs = std::filesystem;
namespace am = launcher::app_menu;

namespace
{
    std::string Read(const fs::path& p)
    {
        std::ifstream f(p, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(f), {});
    }
    bool Has(const std::string& text, const std::string& line) { return text.find("\n" + line + "\n") != std::string::npos; }
}

int main(int argc, char** argv)
{
    if (argc < 2)
        return 2;
    const fs::path scratch = fs::absolute(argv[1]);
    fs::remove_all(scratch);
    fs::create_directories(scratch);
    std::string error;

    // The data home: XDG_DATA_HOME when absolute, else ~/.local/share.
    CHECK(am::DataHome("/x/data", "/home/a") == fs::path("/x/data"), "XDG_DATA_HOME");
    CHECK(am::DataHome(nullptr, "/home/a") == fs::path("/home/a/.local/share"), "the default");
    CHECK(am::DataHome("relative", "/home/a") == fs::path("/home/a/.local/share"), "a relative XDG_DATA_HOME is ignored");
    CHECK(am::DataHome("", "/home/a") == fs::path("/home/a/.local/share"), "an empty one too");
    CHECK(!am::DataHome(nullptr, nullptr) && !am::DataHome(nullptr, "") && !am::DataHome(nullptr, "rel"), "no home");

    const am::Paths paths = am::PathsIn(scratch / "share");
    CHECK(paths.entry == scratch / "share/applications/speedbreaker.desktop", "the entry: %s", paths.entry.c_str());
    CHECK(paths.icon == scratch / "share/icons/speedbreaker-nfsmw.png", "the icon: %s", paths.icon.c_str());

    // Exec: the AppImage quoted, the characters the spec reserves escaped
    // (twice for a backslash: once in the quotes, once in the file), % doubled.
    CHECK(am::ExecLine("/home/a/Games/SpeedBreaker.AppImage") == "\"/home/a/Games/SpeedBreaker.AppImage\"", "plain");
    CHECK(am::ExecLine("/a b/$x`y\"z%1\\w") == "\"/a b/\\$x\\`y\\\"z%%1\\\\\\\\w\"", "escaped: %s",
        am::ExecLine("/a b/$x`y\"z%1\\w").c_str());

    // Nothing there yet.
    CHECK(am::Check(paths) == am::State::None, "none");
    const std::vector<uint8_t> png = { 0x89, 'P', 'N', 'G', 1, 2, 3 };

    // Add: the entry and its icon.
    CHECK(am::Write(paths, "/apps/SpeedBreaker-0.2.5.AppImage", png, error), "added: %s", error.c_str());
    CHECK(am::Check(paths) == am::State::Ours, "ours");
    std::string entry = Read(paths.entry);
    CHECK(entry.rfind("[Desktop Entry]\n", 0) == 0, "a desktop entry");
    CHECK(Has(entry, "Type=Application") && Has(entry, "Name=Need for Speed: Most Wanted") && Has(entry, "Categories=Game;"),
        "named, a game");
    CHECK(Has(entry, "Exec=\"/apps/SpeedBreaker-0.2.5.AppImage\""), "runs the AppImage");
    CHECK(Has(entry, "Icon=" + paths.icon.string()), "its icon");
    CHECK(Has(entry, "X-SpeedBreaker-Menu=1"), "marked as SpeedBreaker's");
    CHECK(Read(paths.icon) == std::string(png.begin(), png.end()), "the icon written");
    CHECK(!fs::exists(paths.entry.string() + ".partial") && !fs::exists(paths.icon.string() + ".partial"), "no partial files");

    // Writing the same again changes nothing; a moved AppImage or a new icon is written.
    const auto stamp = fs::last_write_time(paths.entry);
    bool changed = true;
    CHECK(am::Write(paths, "/apps/SpeedBreaker-0.2.5.AppImage", png, error, &changed) && !changed, "unchanged");
    CHECK(fs::last_write_time(paths.entry) == stamp, "not rewritten");
    CHECK(am::Write(paths, "/apps/SpeedBreaker-0.2.6.AppImage", png, error, &changed) && changed, "moved");
    CHECK(Has(Read(paths.entry), "Exec=\"/apps/SpeedBreaker-0.2.6.AppImage\""), "points at the new one");
    const std::vector<uint8_t> png2 = { 0x89, 'P', 'N', 'G', 9 };
    CHECK(am::Write(paths, "/apps/SpeedBreaker-0.2.6.AppImage", png2, error, &changed) && changed, "new icon");
    CHECK(Read(paths.icon) == std::string(png2.begin(), png2.end()), "the new icon");

    // A path that can't be a desktop entry's: a newline.
    CHECK(!am::Write(paths, "/apps/a\nb.AppImage", png, error) && !error.empty(), "a newline is refused");
    CHECK(Has(Read(paths.entry), "Exec=\"/apps/SpeedBreaker-0.2.6.AppImage\""), "the entry kept");

    // Remove: both files go.
    CHECK(am::Remove(paths, error), "removed: %s", error.c_str());
    CHECK(am::Check(paths) == am::State::None && !fs::exists(paths.icon), "gone");
    CHECK(am::Remove(paths, error), "removing nothing is fine");

    // Someone else's speedbreaker.desktop: never overwritten or removed.
    fixture::WriteBytes(paths.entry, "[Desktop Entry]\nType=Application\nName=Mine\nExec=mine\n");
    CHECK(am::Check(paths) == am::State::Other, "other");
    CHECK(!am::Write(paths, "/apps/S.AppImage", png, error) && !error.empty(), "not overwritten");
    CHECK(!am::Remove(paths, error) && !error.empty(), "not removed");
    CHECK(Read(paths.entry).find("Name=Mine") != std::string::npos, "still theirs");

    // A folder that can't be written: an error, not a crash.
    const am::Paths blocked = am::PathsIn(scratch / "file-not-dir");
    fixture::WriteBytes(scratch / "file-not-dir", "x");
    CHECK(!am::Write(blocked, "/apps/S.AppImage", png, error) && !error.empty(), "unwritable: an error");

    if (g_failures)
        printf("launcher_app_menu_test: %d failure(s)\n", g_failures);
    else
        printf("launcher_app_menu_test: all passed\n");
    return g_failures ? 1 : 0;
}

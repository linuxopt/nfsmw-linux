// The desktop launcher's rules for what it shows (launcher/core/rules.h):
// Smooth Motion as the game offers it on Linux, the Steam Deck from its DMI
// names, HD Textures when a pack is installed, a running game, Game Mode.
// Argument: a scratch folder.
#include <rules.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

static int g_failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } } while (0)

namespace fs = std::filesystem;
using namespace launcher::rules;

static void Write(const fs::path& p, const std::string& text)
{
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << text;
}

int main(int argc, char** argv)
{
    if (argc < 2)
        return 2;
    fs::path dir = argv[1];
    fs::remove_all(dir);
    fs::create_directories(dir);

    auto sm = [](std::vector<double> hz, bool deck, bool on) { return SmoothMotionShown(hz, deck, on); };
    CHECK(!sm({ 60 }, false, false), "60 Hz");
    CHECK(sm({ 89.9 }, false, false), "90 Hz");
    CHECK(sm({ 119.88 }, false, false), "120 Hz");
    CHECK(!sm({ 144 }, false, false), "144 Hz");
    CHECK(!sm({ 240 }, false, false), "240 Hz");
    CHECK(!sm({ 100 }, false, false), "100 Hz");
    CHECK(sm({ 60, 120 }, false, false), "any screen");
    CHECK(!sm({ 120 }, true, false), "never on a Deck");
    CHECK(sm({ 60 }, false, true), "on: always shown, so it can be turned off");
    CHECK(!sm({}, false, false), "no screens");

    unsetenv("NFSMW_HFR_DECK");
    Write(dir / "dmi1/sys_vendor", "Valve\n");
    Write(dir / "dmi1/product_name", "Galileo\n");
    CHECK(SteamDeck(dir / "dmi1"), "an OLED Deck");
    setenv("NFSMW_HFR_DECK", "1", 1);
    CHECK(!SteamDeck(dir / "dmi1"), "NFSMW_HFR_DECK=1 offers Smooth Motion anyway");
    unsetenv("NFSMW_HFR_DECK");
    Write(dir / "dmi2/sys_vendor", "Valve\n");
    Write(dir / "dmi2/product_name", "Steam Machine\n");
    CHECK(!SteamDeck(dir / "dmi2"), "not a Deck");
    CHECK(!SteamDeck(dir / "nothing"), "no DMI files");

    CHECK(!HdTexturesShown(dir / "user"), "no pack");
    Write(dir / "user/texture-pack/pack.json", "{}");
    CHECK(HdTexturesShown(dir / "user"), "a pack");

    fs::path proc = dir / "proc";
    Write(proc / "self/cmdline", std::string("/usr/bin/x\0", 11));
    Write(proc / "77/cmdline", std::string("/usr/bin/SpeedBreaker-notes\0", 28));
    CHECK(!GameRunning(proc), "no game");
    Write(proc / "123/cmdline", std::string("/lib64/ld-linux-x86-64.so.2\0--library-path\0x\0/tmp/a/lib/SpeedBreaker\0", 70));
    CHECK(GameRunning(proc), "the game through a loader");
    CHECK(!GameRunning(dir / "noproc"), "no proc folder");

    CHECK(GameMode("1", nullptr, nullptr), "forced on");
    CHECK(!GameMode("0", "x", "gamescope"), "forced off");
    CHECK(GameMode(nullptr, "gamescope-0", nullptr), "gamescope's display");
    CHECK(GameMode(nullptr, nullptr, "gamescope"), "gamescope's desktop");
    CHECK(!GameMode(nullptr, "", "KDE"), "a desktop");

    if (g_failures)
        printf("launcher_rules_test: %d failure(s)\n", g_failures);
    else
        printf("launcher_rules_test: all passed\n");
    return g_failures ? 1 : 0;
}

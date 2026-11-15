// The desktop launcher's save profiles (launcher/core/saves.h): every
// release's save folder, each profile a <name>/ folder with its
// <name>.xcontent beside it, the display name from the .xcontent.
// Argument: a scratch folder.
#include <saves.h>

#include "launcher_fixture.h"

static int g_failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } } while (0)

namespace fs = std::filesystem;
using namespace launcher;

int main(int argc, char** argv)
{
    if (argc < 2)
        return 2;
    fs::path user = fs::path(argv[1]) / "user";
    fs::remove_all(argv[1]);

    CHECK(saves::List(user).empty(), "no data folder: no profiles");

    fixture::Profile(user, "save", "TJ", "career");
    fixture::Profile(user, "save", "TJ2", "");
    fixture::Profile(user, "save-pal-de", "Ana", "karriere");
    fixture::WriteBytes(user / "save" / "Orphan.xcontent", fixture::Xcontent(u"Orphan", "Orphan"));  // no folder
    fixture::WriteBytes(user / "save" / "Short.xcontent", "tiny");                                    // not a header
    fs::create_directories(user / "save" / "Short");
    fixture::WriteBytes(user / "save" / "Dlc.xcontent", fixture::Xcontent(u"Dlc", "Dlc", 2));         // not a save
    fs::create_directories(user / "save" / "Dlc");

    std::vector<saves::Profile> list = saves::List(user);
    CHECK(list.size() == 3, "TJ, TJ2 and Ana (%zu)", list.size());
    if (list.size() == 3)
    {
        CHECK(list[0].folder == "save" && list[0].name == "TJ" && list[0].displayName == "TJ", "TJ first");
        CHECK(list[1].folder == "save" && list[1].name == "TJ2", "then TJ2");
        CHECK(list[2].folder == "save-pal-de" && list[2].name == "Ana", "then the German release's Ana");
        CHECK(list[0].lastPlayed == fs::last_write_time(user / "save" / "TJ" / "TJ"), "last played: the newest file");
        CHECK(list[1].lastPlayed == fs::last_write_time(user / "save" / "TJ2"), "an empty profile: its folder's time");
    }

    std::string tj = fixture::Xcontent(u"TJ", "TJ");
    CHECK(saves::DisplayName({ reinterpret_cast<const uint8_t*>(tj.data()), tj.size() }) == "TJ", "TJ's display name");
    std::string accented = fixture::Xcontent(u"Ñandú", "N");
    CHECK(saves::DisplayName({ reinterpret_cast<const uint8_t*>(accented.data()), accented.size() }) == "\xC3\x91" "and\xC3\xBA",
        "UTF-16BE to UTF-8");
    std::string emoji = fixture::Xcontent(u"\U0001F697", "Car");
    CHECK(saves::DisplayName({ reinterpret_cast<const uint8_t*>(emoji.data()), emoji.size() }) == "\xF0\x9F\x9A\x97", "a surrogate pair");
    CHECK(saves::DisplayName({}) == "", "nothing");

    CHECK(std::string(saves::ReleaseLabel("save")) == "USA", "USA");
    CHECK(std::string(saves::ReleaseLabel("save-pal-es")) == "Europe (Spanish)", "PAL ES");
    CHECK(std::string(saves::ReleaseLabel("save-pal-de")) == "Europe (German)", "PAL DE");

    if (g_failures)
        printf("launcher_saves_test: %d failure(s)\n", g_failures);
    else
        printf("launcher_saves_test: all passed\n");
    return g_failures ? 1 : 0;
}

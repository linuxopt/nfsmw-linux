// The desktop launcher's .sbsave archives (launcher/core/sbsave.h): one save
// profile as a plain tar, round trip, and every archive that could write
// outside the save folders or isn't one profile refused before anything is
// written. Argument: a scratch folder.
#include <sbsave.h>

#include "launcher_fixture.h"

static int g_failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } } while (0)

namespace fs = std::filesystem;
using namespace launcher;

namespace
{
    size_t CountFiles(const fs::path& dir)
    {
        size_t n = 0;
        std::error_code ec;
        for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
            n++;
        return n;
    }

    // `archive` is refused by Inspect and by Extract, and Extract writes nothing.
    void Refused(const fs::path& scratch, const char* what, const std::string& archive)
    {
        fs::path a = scratch / "bad.sbsave", dest = scratch / ("dest-" + std::string(what));
        fs::remove_all(dest);
        fs::create_directories(dest);
        fixture::WriteBytes(a, archive);
        sbsave::Info info;
        std::string error;
        CHECK(!sbsave::Inspect(a, info, error) && !error.empty(), "%s: Inspect refuses it", what);
        error.clear();
        CHECK(!sbsave::Extract(a, dest, error) && !error.empty(), "%s: Extract refuses it", what);
        CHECK(CountFiles(dest) == 0, "%s: nothing written (%zu)", what, CountFiles(dest));
        CHECK(!fs::exists(scratch / "evil"), "%s: nothing outside", what);
    }
}

int main(int argc, char** argv)
{
    if (argc < 2)
        return 2;
    fs::path scratch = argv[1];
    fs::remove_all(scratch);
    fs::path user = scratch / "user";
    fixture::Profile(user, "save", "TJ", std::string(62688, 'c'));
    fixture::Profile(user, "save-pal-es", "Lu", "partida");

    // Round trip.
    saves::Profile tj{ "save", "TJ", "TJ", {} };
    std::string error;
    fs::path out = scratch / "TJ.sbsave";
    CHECK(sbsave::Write(user, tj, out, error), "written: %s", error.c_str());
    CHECK(!fs::exists(out.string() + ".partial"), "no partial file left");
    sbsave::Info info;
    CHECK(sbsave::Inspect(out, info, error) && info.folder == "save" && info.name == "TJ" && info.bytes >= 62688, "inspected: %s", error.c_str());
    fs::path other = scratch / "other";
    CHECK(sbsave::Extract(out, other, error), "extracted: %s", error.c_str());
    CHECK(fixture::ReadBytes(other / "save" / "TJ" / "TJ") == fixture::ReadBytes(user / "save" / "TJ" / "TJ"), "the save, byte for byte");
    CHECK(fixture::ReadBytes(other / "save" / "TJ.xcontent") == fixture::ReadBytes(user / "save" / "TJ.xcontent"), "its header too");
    CHECK(CountFiles(other / "save") == 3, "nothing else (no staging folder left)");

    // The release goes with it.
    saves::Profile lu{ "save-pal-es", "Lu", "Lu", {} };
    fs::path luOut = scratch / "Lu.sbsave";
    CHECK(sbsave::Write(user, lu, luOut, error) && sbsave::Inspect(luOut, info, error) && info.folder == "save-pal-es", "PAL ES kept");

    // Extract never replaces a profile that's there.
    fs::path again = scratch / "again";
    fixture::Profile(again, "save", "TJ", "mine");
    CHECK(!sbsave::Extract(out, again, error) && !error.empty(), "an existing TJ isn't replaced");
    CHECK(fixture::ReadBytes(again / "save" / "TJ" / "TJ") == "mine", "and stays as it was");

    // Refusals.
    const std::string xc = fixture::Xcontent(u"TJ", "TJ");
    const std::string good = fixture::TarFile("save/TJ.xcontent", xc) + fixture::TarHeader("save/TJ/", '5', 0) + fixture::TarFile("save/TJ/TJ", "x");
    {
        // The good one is accepted (so the refusals below are about their one change).
        fixture::WriteBytes(scratch / "good.sbsave", good + fixture::TarEnd());
        CHECK(sbsave::Inspect(scratch / "good.sbsave", info, error), "the hand-built archive is fine: %s", error.c_str());
    }
    Refused(scratch, "dotdot", good + fixture::TarFile("save/TJ/../../../evil", "x") + fixture::TarEnd());
    Refused(scratch, "absolute", fixture::TarFile("/tmp/evil", "x") + good + fixture::TarEnd());
    Refused(scratch, "symlink", good + fixture::TarHeader("save/TJ/link", '2', 0) + fixture::TarEnd());
    Refused(scratch, "two-profiles", good + fixture::TarFile("save/TJ2.xcontent", xc) + fixture::TarEnd());
    Refused(scratch, "other-folder", fixture::TarFile("saves/TJ.xcontent", xc) + fixture::TarHeader("saves/TJ/", '5', 0) + fixture::TarEnd());
    Refused(scratch, "huge", fixture::TarFile("save/TJ.xcontent", xc) + fixture::TarHeader("save/TJ/TJ", '0', 4ull << 30) + fixture::TarEnd());
    Refused(scratch, "truncated", fixture::TarFile("save/TJ.xcontent", xc) + fixture::TarHeader("save/TJ/TJ", '0', 5000) + std::string(100, 'x'));
    Refused(scratch, "checksum", fixture::TarFile("save/TJ.xcontent", xc) + fixture::TarHeader("save/TJ/", '5', 0, true) + fixture::TarEnd());
    Refused(scratch, "hidden-name", fixture::TarFile("save/.TJ.xcontent", xc) + fixture::TarHeader("save/.TJ/", '5', 0) + fixture::TarEnd());
    Refused(scratch, "no-header", fixture::TarHeader("save/TJ/", '5', 0) + fixture::TarFile("save/TJ/TJ", "x") + fixture::TarEnd());
    Refused(scratch, "no-folder", fixture::TarFile("save/TJ.xcontent", xc) + fixture::TarEnd());
    Refused(scratch, "empty", fixture::TarEnd());
    Refused(scratch, "not-tar", std::string(2000, 'z'));
    Refused(scratch, "duplicate", good + fixture::TarFile("save/TJ/TJ", "y") + fixture::TarEnd());
    {
        // Over 64 MB as a file: refused without reading it all.
        fs::path big = scratch / "big.sbsave";
        { std::ofstream f(big, std::ios::binary); f.seekp((64ll << 20) + 1024); f.put(0); }
        CHECK(!sbsave::Inspect(big, info, error) && !error.empty(), "over 64 MB");
        fs::remove(big);
    }

    if (g_failures)
        printf("launcher_sbsave_test: %d failure(s)\n", g_failures);
    else
        printf("launcher_sbsave_test: all passed\n");
    return g_failures ? 1 : 0;
}

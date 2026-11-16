// The desktop launcher's online settings (launcher/core/online.h): online.toml
// read and written as the game reads it (runtime/net/online.cpp), names and
// servers checked before they're saved. Argument: a scratch folder.
#include <online.h>
#include <net/online.h>

#include "launcher_fixture.h"

static int g_failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } } while (0)

namespace fs = std::filesystem;
namespace lo = launcher::online;  // (::online is the game's, net/online.h)

int main(int argc, char** argv)
{
    if (argc < 2)
        return 2;
    fs::path user = argv[1];
    fs::remove_all(user);
    fs::create_directories(user);
    std::string error;

    // The launcher offers the server the game uses by default.
    CHECK(std::string(launcher::online::kDefaultServer) == ::online::kDefaultServer, "one default server");

    // No file: off, with the default server.
    lo::Config c;
    CHECK(lo::Load(user, c, error), "no file is fine: %s", error.c_str());
    CHECK(!c.enabled && c.name.empty() && c.server == lo::kDefaultServer && c.id.empty(), "off, default server");

    // A file written by hand (no `enabled`): on, as the game takes it.
    fixture::WriteBytes(user / "online.toml", "name = \"Alice\"\n");
    CHECK(lo::Load(user, c, error) && c.enabled && c.name == "Alice" && c.server == lo::kDefaultServer,
        "a hand-written file is on");

    // One the game filled in: its id is kept through a save.
    fixture::WriteBytes(user / "online.toml",
        "id = '0009ABCDEF012345'\nname = 'Alice'\nserver = 'example.org:4000'\nenabled = true\n");
    CHECK(lo::Load(user, c, error) && c.enabled && c.server == "example.org:4000" && c.id == "0009ABCDEF012345",
        "everything read");
    c.enabled = false;
    c.name = "Bob";
    CHECK(lo::Save(user, c, error), "saved: %s", error.c_str());
    CHECK(!fs::exists(user / "online.toml.partial"), "no partial file left");
    lo::Config back;
    CHECK(lo::Load(user, back, error) && !back.enabled && back.name == "Bob" && back.server == "example.org:4000" &&
              back.id == "0009ABCDEF012345",
        "off, renamed, same server and id");

    // A name with quotes and backslashes survives the round trip.
    c.name = "a\"b\\c";
    CHECK(lo::NameProblem(c.name).empty(), "quotes are fine in a name");
    CHECK(lo::Save(user, c, error) && lo::Load(user, back, error) && back.name == "a\"b\\c", "escaped");

    // A broken file: an error, and the launcher won't overwrite it unasked.
    fixture::WriteBytes(user / "online.toml", "name = \"unterminated\n");
    CHECK(!lo::Load(user, c, error) && !error.empty(), "a broken file is reported");

    // Names: 1-15 printable ASCII characters, no leading or trailing space.
    CHECK(lo::NameProblem("TJ").empty(), "TJ");
    CHECK(lo::NameProblem("Racer-1234_x.y").empty(), "punctuation");
    CHECK(lo::NameProblem(std::string(15, 'a')).empty(), "15 characters");
    CHECK(!lo::NameProblem("").empty(), "empty");
    CHECK(!lo::NameProblem(std::string(16, 'a')).empty(), "16 characters");
    CHECK(!lo::NameProblem(" TJ").empty() && !lo::NameProblem("TJ ").empty(), "spaces at the ends");
    CHECK(!lo::NameProblem("J\xC3\xBCrgen").empty(), "non-ASCII (the game shows ASCII only)");
    CHECK(!lo::NameProblem("a\tb").empty(), "control characters");

    // Servers: host:port.
    CHECK(lo::ServerProblem("62.3.24.62:39300").empty(), "an address");
    CHECK(lo::ServerProblem("race.example.org:39300").empty(), "a host name");
    CHECK(lo::ServerProblem("[2001:db8::1]:39300").empty(), "IPv6 in brackets");
    CHECK(!lo::ServerProblem("").empty(), "empty");
    CHECK(!lo::ServerProblem("example.org").empty(), "no port");
    CHECK(!lo::ServerProblem("example.org:0").empty() && !lo::ServerProblem("example.org:70000").empty(),
        "port out of range");
    CHECK(!lo::ServerProblem("example.org:39300x").empty(), "port not a number");
    CHECK(!lo::ServerProblem("exa mple.org:39300").empty(), "a space");
    CHECK(!lo::ServerProblem(":39300").empty(), "no host");

    if (g_failures)
        printf("launcher_online_test: %d failure(s)\n", g_failures);
    else
        printf("launcher_online_test: all passed\n");
    return g_failures ? 1 : 0;
}

// SpeedBreaker tests. GPL-3.0-or-later (see COPYING).
//
// The disc's file archive as texpack/archive.h reads it: NFS/ZDIR.BIN's
// 24-byte little-endian entries {name hash, archive, sector, another
// sector count (not used), size, checksum},
// each file at ZZDATA<archive>.BIN offset sector * 0x800. Synthetic files
// in a temporary folder: no game data.
//
// Build by hand: c++ -std=c++20 -O2 -UNDEBUG -I runtime -I . tests/texpack_archive_test.cpp texpack/archive.cpp -o build/texpack_archive_test
#include <texpack/archive.h>

#include <cstdio>
#include <unistd.h>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

static int s_failures = 0;
#define CHECK(cond, what)                                              \
    do                                                                 \
    {                                                                  \
        if (!(cond))                                                   \
        {                                                              \
            s_failures++;                                              \
            fprintf(stderr, "FAIL %s:%d: %s: %s\n", __FILE__, __LINE__, #cond, what); \
        }                                                              \
    } while (0)

namespace fs = std::filesystem;

static void Put32(std::vector<uint8_t>& v, uint32_t x)
{
    for (int i = 0; i < 4; i++)
        v.push_back(uint8_t(x >> (8 * i)));
}

static void Entry(std::vector<uint8_t>& zdir, uint32_t hash, uint32_t archive, uint32_t sector, uint32_t size)
{
    Put32(zdir, hash);
    Put32(zdir, archive);
    Put32(zdir, sector);
    Put32(zdir, 0x7777);  // not the size: the disc's index has another number here
    Put32(zdir, size);
    Put32(zdir, 0xC0FFEE);
}

static void Write(const fs::path& p, const std::vector<uint8_t>& v)
{
    std::ofstream(p, std::ios::binary).write(reinterpret_cast<const char*>(v.data()), std::streamsize(v.size()));
}

int main()
{
    const fs::path dir = fs::temp_directory_path() / ("texpack_archive_test_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);

    std::string error;
    texpack::Archive missing;
    CHECK(!missing.Open(dir, error) && !error.empty(), "no ZDIR.BIN: refused with a reason");

    // ZZDATA0: file A at sector 0 (5 bytes), file B at sector 2 (3 bytes).
    std::vector<uint8_t> data(0x800 * 3, 0xEE);
    memcpy(&data[0], "hello", 5);
    memcpy(&data[0x1000], "abc", 3);
    Write(dir / "ZZDATA0.BIN", data);
    std::vector<uint8_t> zdir;
    Entry(zdir, 0x10, 0, 0, 5);
    Entry(zdir, 0x20, 0, 2, 3);
    Entry(zdir, 0x30, 0, 2, 0x10000);  // runs past the end of ZZDATA0
    Write(dir / "ZDIR.BIN", zdir);

    texpack::Archive a;
    CHECK(a.Open(dir, error), error.c_str());
    CHECK(a.Entries().size() == 3, "three entries");
    std::vector<uint8_t> out;
    CHECK(a.Entries().size() == 3 && a.Read(a.Entries()[0], out) && out.size() == 5 && memcmp(out.data(), "hello", 5) == 0, "file A");
    CHECK(a.Entries().size() == 3 && a.Read(a.Entries()[1], out) && out.size() == 3 && memcmp(out.data(), "abc", 3) == 0, "file B");
    CHECK(a.Entries().size() == 3 && !a.Read(a.Entries()[2], out), "an entry past its archive's end is skipped, not fatal");
    CHECK(a.Entries().size() == 3 && a.Entries()[1].nameHash == 0x20 && a.Entries()[1].sector == 2, "fields");

    // A ZDIR.BIN whose size isn't a whole number of entries is refused.
    zdir.push_back(0);
    Write(dir / "ZDIR.BIN", zdir);
    texpack::Archive bad;
    CHECK(!bad.Open(dir, error), "a torn ZDIR.BIN");

    fs::remove_all(dir);
    if (s_failures)
        fprintf(stderr, "%d failure(s)\n", s_failures);
    else
        printf("texpack_archive_test: all passed\n");
    return s_failures ? 1 : 0;
}

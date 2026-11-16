// The game's own icon from default.xex (install/title_icon.h): the XDBF
// image table, the XEX resource header, malformed input, and (when the
// argument names a default.xex that exists) the real game's 64x64 PNG.
#include <install/title_icon.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int g_failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } } while (0)

namespace
{
    using Bytes = std::vector<uint8_t>;
    void Put16(Bytes& b, uint16_t v) { b.push_back(uint8_t(v >> 8)); b.push_back(uint8_t(v)); }
    void Put32(Bytes& b, uint32_t v) { for (int s = 24; s >= 0; s -= 8) b.push_back(uint8_t(v >> s)); }
    void Put64(Bytes& b, uint64_t v) { Put32(b, uint32_t(v >> 32)); Put32(b, uint32_t(v)); }

    Bytes Png(uint8_t tag)
    {
        Bytes p = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
        p.insert(p.end(), { tag, tag, tag, tag });
        return p;
    }

    struct Item { uint16_t ns; uint64_t id; Bytes data; };

    // An XDBF file with `items`, a table of `slots` entries and one free entry.
    Bytes Xdbf(const std::vector<Item>& items, uint32_t slots)
    {
        Bytes b(4);
        std::memcpy(b.data(), "XDBF", 4);
        Put32(b, 0x10000);
        Put32(b, slots);
        Put32(b, uint32_t(items.size()));
        Put32(b, 1);
        Put32(b, 0);
        Bytes data;
        for (const Item& it : items)
        {
            Put16(b, it.ns);
            Put64(b, it.id);
            Put32(b, uint32_t(data.size()));
            Put32(b, uint32_t(it.data.size()));
            data.insert(data.end(), it.data.begin(), it.data.end());
        }
        b.resize(24 + slots * 18 + 8, 0);
        b.insert(b.end(), data.begin(), data.end());
        return b;
    }

    // A XEX header whose resource table lists `resources` (address, size).
    Bytes Xex(const std::vector<std::pair<uint32_t, uint32_t>>& resources)
    {
        Bytes b(4);
        std::memcpy(b.data(), "XEX2", 4);
        Put32(b, 0);
        Put32(b, 0x1000);
        Put32(b, 0);
        Put32(b, 0x100);
        Put32(b, 2);
        Put32(b, 0x00010001);  // another header first (a value, not an offset)
        Put32(b, 0x82000000);
        Put32(b, 0x000002FF);
        Put32(b, 40);          // the resource table, right after these two
        Put32(b, uint32_t(4 + 16 * resources.size()));
        for (auto [address, size] : resources)
        {
            b.insert(b.end(), { '4', '5', '4', '1', '0', '7', 'D', '9' });
            Put32(b, address);
            Put32(b, size);
        }
        return b;
    }
}

int main(int argc, char** argv)
{
    const uint64_t id = install::kTitleIconId;

    // The image in namespace 2; the same id in another namespace (a string) is not it.
    Bytes xdbf = Xdbf({ { 1, id, { 'n', 'o' } }, { 2, 1, Png(1) }, { 2, id, Png(7) } }, 5);
    CHECK(install::XdbfImage(xdbf, id) == Png(7), "the 0x8000 image");
    CHECK(install::XdbfImage(xdbf, 1) == Png(1), "another image by id");
    CHECK(install::XdbfImage(xdbf, 2).empty(), "an id it doesn't have");

    // Malformed: wrong magic, truncated, an entry past the end, not a PNG,
    // more entries used than the table holds.
    Bytes bad = xdbf;
    bad[0] = 'Y';
    CHECK(install::XdbfImage(bad, id).empty(), "not XDBF");
    CHECK(install::XdbfImage(std::span(xdbf).first(30), id).empty(), "truncated in the table");
    CHECK(install::XdbfImage(std::span(xdbf).first(xdbf.size() - 2), id).empty(), "the image cut short");
    CHECK(install::XdbfImage(Xdbf({ { 2, id, { 'G', 'I', 'F', '8', '9', 'a', 0, 0, 0 } } }, 1), id).empty(), "not a PNG");
    bad = Xdbf({ { 2, id, Png(7) } }, 1);
    bad[15] = 9;
    CHECK(install::XdbfImage(bad, id).empty(), "used > entries");
    CHECK(install::XdbfImage({}, id).empty(), "empty");

    // Through the XEX: the resource that is an XDBF with the icon, at its
    // address in the image; resources outside the image are skipped.
    const uint64_t base = 0x82000000;
    Bytes image(0x400, 0xEE);
    std::memcpy(&image[0x200], xdbf.data(), xdbf.size());
    const uint32_t at = uint32_t(base + 0x200), size = uint32_t(xdbf.size());
    CHECK(install::TitleIcon(Xex({ { at, size } }), image, base) == Png(7), "found through the resource table");
    CHECK(install::TitleIcon(Xex({ { uint32_t(base + 0x380), 0x100 }, { at, size } }), image, base) == Png(7),
        "a resource running past the image is skipped");
    CHECK(install::TitleIcon(Xex({ { uint32_t(base - 0x10), 8 }, { at, size } }), image, base) == Png(7),
        "a resource below the image is skipped");
    CHECK(install::TitleIcon(Xex({ { uint32_t(base + 0x10), 0x40 } }), image, base).empty(), "no XDBF resource");
    CHECK(install::TitleIcon(Xex({}), image, base).empty(), "no resources");
    Bytes xex = Xex({ { at, size } });
    CHECK(install::TitleIcon(std::span(xex).first(40), image, base).empty(), "header cut short");
    xex[20] = 0x7F;
    CHECK(install::TitleIcon(xex, image, base).empty(), "a header count past the end");
    CHECK(install::TitleIconFromXex("/nonexistent/default.xex").empty(), "no file");

    // The real game, when there is one: its dashboard icon, a 64x64 PNG.
    if (argc > 1)
    {
        if (FILE* f = fopen(argv[1], "rb"))
        {
            fclose(f);
            Bytes icon = install::TitleIconFromXex(argv[1]);
            CHECK(icon.size() > 24 && std::memcmp(icon.data() + 12, "IHDR", 4) == 0, "a PNG (%zu bytes)", icon.size());
            if (icon.size() > 24)
            {
                const uint32_t w = uint32_t(icon[16]) << 24 | icon[17] << 16 | icon[18] << 8 | icon[19];
                const uint32_t h = uint32_t(icon[20]) << 24 | icon[21] << 16 | icon[22] << 8 | icon[23];
                CHECK(w == 64 && h == 64, "64x64 (%ux%u)", w, h);
            }
        }
        else
            printf("title_icon_test: no %s, the real game's icon not checked\n", argv[1]);
    }

    if (g_failures)
        printf("title_icon_test: %d failure(s)\n", g_failures);
    else
        printf("title_icon_test: all passed\n");
    return g_failures ? 1 : 0;
}

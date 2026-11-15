// Fixtures for the desktop launcher's save tests: save profiles laid out as
// the game writes them (kernel/xam.cpp: <folder>/<name>/ and
// <folder>/<name>.xcontent, a 308-byte XCONTENT_DATA), and hand-built tar
// archives for the .sbsave refusals.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fixture
{
    namespace fs = std::filesystem;

    inline void WriteBytes(const fs::path& p, const std::string& bytes)
    {
        fs::create_directories(p.parent_path());
        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        f << bytes;
    }

    inline std::string ReadBytes(const fs::path& p)
    {
        std::ifstream f(p, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(f), {});
    }

    // An XCONTENT_DATA as the game writes it: DeviceID 1, type `type`
    // (1: a save), the display name in UTF-16BE then 0xAAAA filler (the
    // real TJ.xcontent's), the file name, padded to 308 bytes.
    inline std::string Xcontent(const std::u16string& display, const std::string& fileName, uint32_t type = 1)
    {
        std::string b(308, char(0xAA));
        auto be32 = [&](size_t at, uint32_t v) { for (int i = 0; i < 4; i++) b[at + i] = char(v >> (24 - 8 * i)); };
        be32(0, 1);
        be32(4, type);
        size_t at = 8;
        for (char16_t c : display)
        {
            b[at++] = char(c >> 8);
            b[at++] = char(c & 0xFF);
        }
        b[at++] = 0;
        b[at++] = 0;
        std::memset(&b[264], 0, 42);
        std::memcpy(&b[264], fileName.data(), std::min<size_t>(fileName.size(), 41));
        return b;
    }

    // A profile: <user>/<folder>/<name>.xcontent and <name>/<name> (the
    // game's one file, `body`).
    inline void Profile(const fs::path& user, const std::string& folder, const std::string& name, const std::string& body,
        const std::u16string& display = u"")
    {
        std::u16string d = display.empty() ? std::u16string(name.begin(), name.end()) : display;
        WriteBytes(user / folder / (name + ".xcontent"), Xcontent(d, name));
        fs::create_directories(user / folder / name);
        if (!body.empty())
            WriteBytes(user / folder / name / name, body);
    }

    // A ustar header (512 bytes) for `name`, type `type` ('0' file, '5'
    // dir, '2' symlink), `size` (in the octal field), with a correct
    // checksum unless `badChecksum`.
    inline std::string TarHeader(const std::string& name, char type, uint64_t size, bool badChecksum = false)
    {
        std::string h(512, '\0');
        std::memcpy(&h[0], name.data(), std::min<size_t>(name.size(), 100));
        std::snprintf(&h[100], 8, "%07o", type == '5' ? 0755u : 0644u);
        std::snprintf(&h[108], 8, "%07o", 0u);
        std::snprintf(&h[116], 8, "%07o", 0u);
        char sz[13];
        std::snprintf(sz, sizeof sz, "%011llo", (unsigned long long)size);
        std::memcpy(&h[124], sz, 11);
        std::snprintf(&h[136], 12, "%011o", 0u);
        h[156] = type;
        std::memcpy(&h[257], "ustar", 6);
        std::memcpy(&h[263], "00", 2);
        std::memset(&h[148], ' ', 8);
        unsigned sum = 0;
        for (unsigned char c : h)
            sum += c;
        if (badChecksum)
            sum += 1;
        std::snprintf(&h[148], 8, "%06o", sum);
        h[155] = ' ';
        return h;
    }

    // A file entry: header plus the body padded to 512.
    inline std::string TarFile(const std::string& name, const std::string& body)
    {
        std::string e = TarHeader(name, '0', body.size()) + body;
        e.resize((e.size() + 511) / 512 * 512, '\0');
        return e;
    }

    inline std::string TarEnd() { return std::string(1024, '\0'); }
}

// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// The game's own icon, from the player's default.xex: an Xbox 360 title
// carries its dashboard art in a resource named by its title id (an XDBF
// file: the title's achievements, strings and images), and image 0x8000 in
// it is the icon the dashboard showed for the game (a 64x64 PNG). The game
// window and the launcher use it, so SpeedBreaker ships no game art: each
// player's copy supplies its own.
#pragma once
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace install
{
    inline constexpr uint64_t kTitleIconId = 0x8000;

    // Image `id` (namespace 2) of an XDBF file, or empty when it has none or
    // the file is malformed.
    std::vector<uint8_t> XdbfImage(std::span<const uint8_t> xdbf, uint64_t id);

    // The title icon (a PNG) of a loaded XEX: `xexFile` is the raw file (its
    // resource header), `image` its loaded image at `imageBase`. Empty when
    // there is none.
    std::vector<uint8_t> TitleIcon(std::span<const uint8_t> xexFile, std::span<const uint8_t> image, uint64_t imageBase);

    // Reads and loads `xex` (decryption, decompression: about 0.1 s for the
    // game's) for its title icon. Empty when it can't be read or isn't a XEX.
    std::vector<uint8_t> TitleIconFromXex(const std::filesystem::path& xex);
}

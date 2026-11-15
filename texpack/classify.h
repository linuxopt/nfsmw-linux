// SpeedBreaker texture pack tools. GPL-3.0-or-later (see COPYING).
//
// Which textures go to the AI upscaler. Color: game art, upscaled. Ui: the
// front end, HUD and fonts, upscaled too but a group the build can leave
// out (--no-ui). Normal: normal and bump maps, never (an image model would
// bend their vectors). Data: lookup tables, ramps, tiny or single-channel
// and float textures, never. Captured textures carry no names: they're
// Color unless their format or size says otherwise. Normal maps without a
// telling name are caught by their colour (LooksLikeNormalMap) once decoded.
#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace texpack
{
    enum class TexClass
    {
        Color,
        Ui,
        Normal,
        Data
    };

    inline bool ContainsNoCase(std::string_view s, std::string_view word)
    {
        auto it = std::search(s.begin(), s.end(), word.begin(), word.end(),
            [](char a, char b) { return std::toupper(uint8_t(a)) == std::toupper(uint8_t(b)); });
        return it != s.end();
    }

    inline TexClass Classify(uint32_t format, uint32_t width, uint32_t height, std::string_view name, std::string_view tpkName)
    {
        if (format == 49 || ContainsNoCase(name, "NORMAL") || ContainsNoCase(name, "BUMP") || ContainsNoCase(name, "_NM"))
            return TexClass::Normal;
        const bool colourFormat = format == 3 || format == 4 || format == 6 || format == 14 || format == 15 || format == 18 ||
            format == 19 || format == 20;
        if (!colourFormat || width <= 1 || height <= 1 || (width <= 16 && height <= 16))
            return TexClass::Data;
        if (ContainsNoCase(name, "FONT") || ContainsNoCase(tpkName, "FRONTEND") || ContainsNoCase(tpkName, "HUD") ||
            ContainsNoCase(tpkName, "FONT"))
            return TexClass::Ui;
        return TexClass::Color;
    }

    // A tangent-space normal map by its colour, for those without a telling
    // name: over its opaque texels, red and green average near the middle
    // (128 +- 20) and blue is high (over 200). Colour art, even blue art,
    // doesn't sit there.
    inline bool LooksLikeNormalMap(const std::vector<uint8_t>& rgba)
    {
        double sum[3] = {};
        size_t n = 0;
        for (size_t i = 0; i + 3 < rgba.size(); i += 4)
        {
            if (rgba[i + 3] < 128)
                continue;
            for (int c = 0; c < 3; c++)
                sum[c] += rgba[i + c];
            n++;
        }
        if (n == 0)
            return false;
        const double r = sum[0] / n, g = sum[1] / n, b = sum[2] / n;
        return r > 108 && r < 148 && g > 108 && g < 148 && b > 200;
    }
}

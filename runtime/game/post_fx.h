// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// Haze (Settings > Graphics): how far the game's colour treatment lifts the
// picture's dark end, the grey veil over Most Wanted's scenes.
//
// The game's post-processing (visualtreatment.fx) passes the scene through a
// tone curve, CURVE_COEFFS_0-3 (a cubic per channel). Its visual treatment
// object (the time of day's look, updated every frame) holds the curve's four
// control points as floats at +20, +36, +52 and +68: (0.15, 0.35, 0.98, 1.0)
// in a day race, where a neutral curve would be (0, 1/3, 2/3, 1). The first is
// the output for black: the scene's blacks come out 15% grey, its shadows and
// mid-tones lifted with them. sub_82223BB8 (called by the treatment's pass,
// sub_822246F0, each frame) turns the points into the curve's coefficients
// (sub_82224C78, sub_82223AA0) and sets the shader's constants.
//
// The runtime scales that first point by the setting around the call (100%:
// the game's own; 0%: blacks stay black), and puts the game's value back
// after it, so the time of day's own changes go on as before. Measured on a
// race start: at 0% the darkest quarter of the picture is 7 levels (of 255)
// darker and the brightest parts are unchanged.
// (The constant g_fBloomScale, 0x82908AB4, changes nothing visible in this
// path: the bloom's strength isn't adjustable this way.)
#pragma once
#include <cstdint>

namespace game::post_fx
{
    constexpr uint32_t kCurveBlack = 20;  // offset of the curve's first control point in the treatment object

    // The black point used for a Haze of `percent` (0-100) given the game's.
    constexpr float HazeBlack(float game, int percent)
    {
        return game * float(percent < 0 ? 0 : percent > 100 ? 100 : percent) / 100.0f;
    }
}

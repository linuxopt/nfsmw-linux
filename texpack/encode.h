// SpeedBreaker texture pack tools. GPL-3.0-or-later (see COPYING).
//
// The pack's encoder: an original texture's PNG and its AI-upscaled PNG
// become <key>.dds, BC7 (DX10 header, DXGI_FORMAT_BC7_UNORM), with a full
// mip chain, its long side at most `maxSide` (Lanczos-3 down when the 4x
// result is larger). Colour: the original's tones with the upscale's detail
// (detail transfer: the upscale's low frequencies are replaced by the
// original's, so a model can't darken or tint the art); alpha from the original,
// enlarged with Lanczos-3 (the model isn't trusted with cut-out edges), and
// for 1-bit sources thresholded again at every level with the coverage of
// the top one kept, so fences and foliage don't thin out in the distance. A
// result whose mean colour still drifts more than `maxDrift` (per channel,
// 0-1; only clipping can cause it now) is refused (Drift): the original
// stays in use.
// Tiling textures: PadWrapped adds `pad` texels of the texture's own
// opposite edges around it before upscaling, so the model sees the
// neighbours a seam will have; EncodeTexture's `pad` crops them off after.
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>

namespace texpack
{
    struct EncodeOptions
    {
        std::filesystem::path src, up, out;
        std::string alpha = "none";  // none, 1bit, full (manifest.json's)
        uint32_t pad = 0;            // wrapped padding around `up`'s input, in original texels
        uint32_t maxSide = 2048;
        float maxDrift = 4.0f / 255.0f;
    };

    enum class EncodeResult
    {
        Ok,
        Drift,
        Error
    };

    EncodeResult EncodeTexture(const EncodeOptions& options, std::string& info);
    bool PadWrapped(const std::filesystem::path& in, const std::filesystem::path& out, uint32_t pad, std::string& error);
    // RGBA8, Lanczos-3, either direction.
    void ResizeLanczos(const uint8_t* src, uint32_t sw, uint32_t sh, uint8_t* dst, uint32_t dw, uint32_t dh);
}

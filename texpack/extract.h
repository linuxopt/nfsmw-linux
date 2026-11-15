// SpeedBreaker texture pack tools. GPL-3.0-or-later (see COPYING).
//
// Extraction: every texture of the installed disc (plain files and the
// JDLZ-decoded city stream, docs/TEXPACK_FORMAT.md) plus every play-captured
// one (gpu/texpack_capture.h), as one set by key. For the upscaler, each
// colour and UI texture's base level as <work>/src/<key>.png (kept when it
// is already there: a re-run resumes); for the build, <work>/manifest.json
// listing them all with their class, source and alpha.
#pragma once
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>

namespace texpack
{
    struct ExtractOptions
    {
        std::filesystem::path nfsDir;      // the install's NFS folder
        std::filesystem::path captureDir;  // play-capture's folder (may be missing)
        std::filesystem::path workDir;
        std::string region = "usa";
        unsigned jobs = 0;                 // 0: one per hardware thread
        FILE* keysOnly = nullptr;          // set: only list "<key> <tpk> <format> <w>x<h>" lines here
    };

    struct ExtractStats
    {
        uint32_t tpks = 0, textures = 0, unique = 0, fromDisc = 0, fromCapture = 0, undecoded = 0;
        uint32_t color = 0, ui = 0, normal = 0, data = 0, pngs = 0;
        uint32_t jdlzBlocks = 0, otherBlocks = 0;
    };

    bool Extract(const ExtractOptions& options, ExtractStats& stats, std::string& error);
}

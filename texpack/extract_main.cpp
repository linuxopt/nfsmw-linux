// SpeedBreaker texture pack tools. GPL-3.0-or-later (see COPYING).
//
// texpack-extract: the textures of the player's installed disc, plus the
// ones play-capture saved (texpack/extract.h).
//
//   texpack-extract <install>/NFS <work dir> [--capture DIR] [--region R] [--jobs N]
//   texpack-extract --keys-only <install>/NFS [--capture DIR]
//
// The first writes <work>/src/<key>.png for each colour and UI texture and
// <work>/manifest.json; the second lists "<key> <tpk> <format> <w>x<h>".
// A summary goes to stderr.
#include "extract.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    texpack::ExtractOptions o;
    bool keysOnly = false;
    std::vector<std::string> positional;
    for (int i = 1; i < argc; i++)
    {
        const std::string a = argv[i];
        if (a == "--keys-only")
            keysOnly = true;
        else if (a == "--capture" && i + 1 < argc)
            o.captureDir = argv[++i];
        else if (a == "--region" && i + 1 < argc)
            o.region = argv[++i];
        else if (a == "--jobs" && i + 1 < argc)
            o.jobs = unsigned(std::stoul(argv[++i]));
        else
            positional.push_back(a);
    }
    if (positional.size() != (keysOnly ? 1u : 2u))
    {
        fprintf(stderr, "usage: texpack-extract <install>/NFS <work dir> [--capture DIR] [--region R] [--jobs N]\n"
                        "       texpack-extract --keys-only <install>/NFS [--capture DIR]\n");
        return 2;
    }
    o.nfsDir = positional[0];
    if (keysOnly)
        o.keysOnly = stdout;
    else
        o.workDir = positional[1];
    texpack::ExtractStats st;
    std::string error;
    if (!texpack::Extract(o, st, error))
    {
        fprintf(stderr, "texpack-extract: %s\n", error.c_str());
        return 1;
    }
    fprintf(stderr,
        "texpack-extract: %u TPKs, %u textures (%u in HUFF blocks not decoded); %u unique: %u from the disc, %u captured; "
        "%u color, %u ui, %u normal, %u data; %u new PNGs; blocks %u JDLZ, %u HUFF\n",
        st.tpks, st.textures, st.undecoded, st.unique, st.fromDisc, st.fromCapture, st.color, st.ui, st.normal, st.data, st.pngs,
        st.jdlzBlocks, st.otherBlocks);
    return 0;
}

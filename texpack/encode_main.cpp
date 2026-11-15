// SpeedBreaker texture pack tools. GPL-3.0-or-later (see COPYING).
//
// texpack-encode: upscaled PNGs to the pack's BC7 DDS files (texpack/encode.h).
//
//   texpack-encode --src A.png --up A_up.png --out A.dds [--alpha none|1bit|full] [--pad N]
//   texpack-encode --batch JOBS.tsv [--jobs N]     lines: src up out alpha pad
//   texpack-encode --pad-batch JOBS.tsv [--jobs N] lines: in out pad (PadWrapped)
//
// One texture: exit 0 written, 3 refused for colour drift, 1 error. A batch
// prints "<out> ok|drift|error <detail>" per line and exits 0 unless the list
// can't be read.
#include "encode.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace
{
    std::vector<std::vector<std::string>> ReadTsv(const std::string& path, size_t fields, bool& ok)
    {
        std::vector<std::vector<std::string>> rows;
        std::ifstream f(path);
        ok = bool(f);
        std::string line;
        while (std::getline(f, line))
        {
            std::vector<std::string> row;
            std::stringstream ss(line);
            std::string cell;
            while (std::getline(ss, cell, '\t'))
                row.push_back(cell);
            if (row.size() == fields)
                rows.push_back(std::move(row));
        }
        return rows;
    }

    template<typename F>
    void Parallel(size_t count, unsigned jobs, F&& each)
    {
        std::atomic<size_t> next{ 0 };
        std::vector<std::thread> threads;
        for (unsigned t = 0; t < std::max(1u, jobs); t++)
            threads.emplace_back([&] {
                for (size_t i; (i = next++) < count;)
                    each(i);
            });
        for (auto& t : threads)
            t.join();
    }
}

int main(int argc, char** argv)
{
    texpack::EncodeOptions o;
    std::string batch, padBatch;
    unsigned jobs = std::thread::hardware_concurrency();
    for (int i = 1; i + 1 < argc; i += 2)
    {
        const std::string a = argv[i], v = argv[i + 1];
        if (a == "--src") o.src = v;
        else if (a == "--up") o.up = v;
        else if (a == "--out") o.out = v;
        else if (a == "--alpha") o.alpha = v;
        else if (a == "--pad") o.pad = uint32_t(std::stoul(v));
        else if (a == "--batch") batch = v;
        else if (a == "--pad-batch") padBatch = v;
        else if (a == "--jobs") jobs = unsigned(std::stoul(v));
        else
        {
            fprintf(stderr, "texpack-encode: unknown option %s\n", a.c_str());
            return 2;
        }
    }
    std::mutex out;
    if (!padBatch.empty())
    {
        bool ok;
        auto rows = ReadTsv(padBatch, 3, ok);
        if (!ok)
            return 1;
        Parallel(rows.size(), jobs, [&](size_t i) {
            std::string error;
            const bool done = texpack::PadWrapped(rows[i][0], rows[i][1], uint32_t(std::stoul(rows[i][2])), error);
            std::lock_guard lock(out);
            printf("%s %s %s\n", rows[i][1].c_str(), done ? "ok" : "error", error.c_str());
        });
        return 0;
    }
    if (!batch.empty())
    {
        bool ok;
        auto rows = ReadTsv(batch, 5, ok);
        if (!ok)
            return 1;
        Parallel(rows.size(), jobs, [&](size_t i) {
            texpack::EncodeOptions e;
            e.src = rows[i][0];
            e.up = rows[i][1];
            e.out = rows[i][2];
            e.alpha = rows[i][3];
            e.pad = uint32_t(std::stoul(rows[i][4]));
            std::string info;
            const texpack::EncodeResult r = texpack::EncodeTexture(e, info);
            std::lock_guard lock(out);
            printf("%s %s %s\n", rows[i][2].c_str(),
                r == texpack::EncodeResult::Ok ? "ok" : r == texpack::EncodeResult::Drift ? "drift" : "error", info.c_str());
            fflush(stdout);
        });
        return 0;
    }
    if (o.src.empty() || o.up.empty() || o.out.empty())
    {
        fprintf(stderr, "usage: texpack-encode --src A.png --up A_up.png --out A.dds [--alpha none|1bit|full] [--pad N]\n"
                        "       texpack-encode --batch JOBS.tsv [--jobs N]\n       texpack-encode --pad-batch JOBS.tsv [--jobs N]\n");
        return 2;
    }
    std::string info;
    const texpack::EncodeResult r = texpack::EncodeTexture(o, info);
    fprintf(stderr, "texpack-encode: %s\n", info.c_str());
    return r == texpack::EncodeResult::Ok ? 0 : r == texpack::EncodeResult::Drift ? 3 : 1;
}

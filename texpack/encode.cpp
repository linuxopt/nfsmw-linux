// SpeedBreaker texture pack tools. GPL-3.0-or-later (see COPYING).
#include "encode.h"

#include "thirdparty/bc7enc/bc7enc.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "thirdparty/stb/stb_image.h"
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "thirdparty/stb/stb_image_write.h"

namespace texpack
{
    namespace
    {
        struct Image
        {
            uint32_t w = 0, h = 0;
            std::vector<uint8_t> px;  // RGBA8
        };

        bool Load(const std::filesystem::path& p, Image& img)
        {
            int w, h, n;
            uint8_t* data = stbi_load(p.c_str(), &w, &h, &n, 4);
            if (!data)
                return false;
            img.w = uint32_t(w);
            img.h = uint32_t(h);
            img.px.assign(data, data + size_t(w) * h * 4);
            stbi_image_free(data);
            return true;
        }

        double Sinc(double x)
        {
            if (x == 0)
                return 1;
            x *= M_PI;
            return std::sin(x) / x;
        }

        // One axis: `count` lines of `n` samples (stride `step` between samples,
        // `lineStep` between lines) to `m` samples.
        void ResizeAxis(const float* src, uint32_t n, float* dst, uint32_t m, uint32_t count, size_t step, size_t lineStep, size_t dstStep,
            size_t dstLineStep)
        {
            const double scale = double(m) / n, filterScale = std::max(1.0, 1.0 / scale), support = 3.0 * filterScale;
            std::vector<double> w;
            for (uint32_t i = 0; i < m; i++)
            {
                const double centre = (i + 0.5) / scale - 0.5;
                const int lo = int(std::floor(centre - support)), hi = int(std::ceil(centre + support));
                w.assign(size_t(hi - lo + 1), 0);
                double sum = 0;
                for (int j = lo; j <= hi; j++)
                {
                    const double x = (j - centre) / filterScale;
                    const double k = std::fabs(x) < 3 ? Sinc(x) * Sinc(x / 3) : 0;
                    w[size_t(j - lo)] = k;
                    sum += k;
                }
                for (uint32_t line = 0; line < count; line++)
                    for (int c = 0; c < 4; c++)
                    {
                        double acc = 0;
                        for (int j = lo; j <= hi; j++)
                        {
                            const int s = std::clamp(j, 0, int(n) - 1);
                            acc += w[size_t(j - lo)] * src[line * lineStep + size_t(s) * step + c];
                        }
                        dst[line * dstLineStep + i * dstStep + c] = float(acc / sum);
                    }
            }
        }

        void Box2(const Image& in, Image& out)
        {
            out.w = std::max(1u, in.w / 2);
            out.h = std::max(1u, in.h / 2);
            out.px.assign(size_t(out.w) * out.h * 4, 0);
            for (uint32_t y = 0; y < out.h; y++)
                for (uint32_t x = 0; x < out.w; x++)
                    for (int c = 0; c < 4; c++)
                    {
                        uint32_t sum = 0;
                        for (uint32_t dy = 0; dy < 2; dy++)
                            for (uint32_t dx = 0; dx < 2; dx++)
                            {
                                const uint32_t sx = std::min(in.w - 1, x * 2 + dx), sy = std::min(in.h - 1, y * 2 + dy);
                                sum += in.px[(size_t(sy) * in.w + sx) * 4 + c];
                            }
                        out.px[(size_t(y) * out.w + x) * 4 + c] = uint8_t((sum + 2) / 4);
                    }
        }

        double Coverage(const Image& img, uint32_t threshold)
        {
            size_t n = 0;
            for (size_t i = 3; i < img.px.size(); i += 4)
                n += img.px[i] >= threshold;
            return double(n) / double(img.px.size() / 4);
        }

        // Re-threshold a 1-bit level's alpha so its coverage matches `target`.
        void KeepCoverage(Image& img, double target)
        {
            uint32_t best = 128;
            double bestErr = 2;
            for (uint32_t t = 1; t < 256; t++)
            {
                const double err = std::fabs(Coverage(img, t) - target);
                if (err < bestErr)
                    bestErr = err, best = t;
            }
            for (size_t i = 3; i < img.px.size(); i += 4)
                img.px[i] = img.px[i] >= best ? 255 : 0;
        }

        void MeanRgb(const Image& img, double out[3])
        {
            double sum[3] = {};
            size_t n = 0;
            for (size_t i = 0; i < img.px.size(); i += 4)
            {
                if (img.px[i + 3] == 0)
                    continue;
                for (int c = 0; c < 3; c++)
                    sum[c] += img.px[i + c];
                n++;
            }
            for (int c = 0; c < 3; c++)
                out[c] = n ? sum[c] / n / 255.0 : 0;
        }

        void Put32(std::vector<uint8_t>& v, size_t at, uint32_t x)
        {
            for (int i = 0; i < 4; i++)
                v[at + i] = uint8_t(x >> (8 * i));
        }

        std::once_flag s_bc7Init;
    }

    void ResizeLanczos(const uint8_t* src, uint32_t sw, uint32_t sh, uint8_t* dst, uint32_t dw, uint32_t dh)
    {
        std::vector<float> in(size_t(sw) * sh * 4), mid(size_t(dw) * sh * 4), out(size_t(dw) * dh * 4);
        for (size_t i = 0; i < in.size(); i++)
            in[i] = src[i];
        ResizeAxis(in.data(), sw, mid.data(), dw, sh, 4, size_t(sw) * 4, 4, size_t(dw) * 4);   // rows
        ResizeAxis(mid.data(), sh, out.data(), dh, dw, size_t(dw) * 4, 4, size_t(dw) * 4, 4);  // columns
        for (size_t i = 0; i < out.size(); i++)
            dst[i] = uint8_t(std::clamp(std::lround(out[i]), 0L, 255L));
    }

    bool PadWrapped(const std::filesystem::path& in, const std::filesystem::path& out, uint32_t pad, std::string& error)
    {
        Image img;
        if (!Load(in, img))
        {
            error = "can't read " + in.string();
            return false;
        }
        const uint32_t w = img.w + 2 * pad, h = img.h + 2 * pad;
        std::vector<uint8_t> px(size_t(w) * h * 4);
        for (uint32_t y = 0; y < h; y++)
            for (uint32_t x = 0; x < w; x++)
            {
                const uint32_t sx = (x + img.w * 4 - pad) % img.w, sy = (y + img.h * 4 - pad) % img.h;
                memcpy(&px[(size_t(y) * w + x) * 4], &img.px[(size_t(sy) * img.w + sx) * 4], 4);
            }
        if (!stbi_write_png(out.c_str(), int(w), int(h), 4, px.data(), int(w * 4)))
        {
            error = "can't write " + out.string();
            return false;
        }
        return true;
    }

    EncodeResult EncodeTexture(const EncodeOptions& o, std::string& info)
    {
        Image src, up;
        if (!Load(o.src, src) || !Load(o.up, up))
        {
            info = "can't read " + o.src.string() + " or " + o.up.string();
            return EncodeResult::Error;
        }
        const uint32_t paddedW = src.w + 2 * o.pad;
        if (up.w % paddedW != 0 || up.w / paddedW == 0 || up.h != (src.h + 2 * o.pad) * (up.w / paddedW))
        {
            info = "the upscale isn't a whole multiple of the original";
            return EncodeResult::Error;
        }
        const uint32_t scale = up.w / paddedW;
        Image top;
        top.w = src.w * scale;
        top.h = src.h * scale;
        top.px.resize(size_t(top.w) * top.h * 4);
        for (uint32_t y = 0; y < top.h; y++)
            memcpy(&top.px[size_t(y) * top.w * 4], &up.px[(size_t(y + o.pad * scale) * up.w + o.pad * scale) * 4], size_t(top.w) * 4);

        // Detail transfer: the original's tones, the upscale's detail. The
        // upscale's low frequencies (it shrunk to the original's size and
        // enlarged again) are swapped for the original's own (enlarged), so a
        // model that darkens or tints can't change the art's colour; only
        // what it adds at fine scale stays. Alpha comes from the original too.
        {
            std::vector<uint8_t> base(top.px.size()), small(src.px.size()), low(top.px.size());
            ResizeLanczos(src.px.data(), src.w, src.h, base.data(), top.w, top.h);
            ResizeLanczos(top.px.data(), top.w, top.h, small.data(), src.w, src.h);
            ResizeLanczos(small.data(), src.w, src.h, low.data(), top.w, top.h);
            for (size_t i = 0; i < top.px.size(); i += 4)
            {
                for (int c = 0; c < 3; c++)
                    top.px[i + c] = uint8_t(std::clamp(int(top.px[i + c]) - int(low[i + c]) + int(base[i + c]), 0, 255));
                top.px[i + 3] = o.alpha == "none" ? 255 : o.alpha == "1bit" ? (base[i + 3] >= 128 ? 255 : 0) : base[i + 3];
            }
        }

        // The cap.
        if (std::max(top.w, top.h) > o.maxSide)
        {
            const double f = double(o.maxSide) / std::max(top.w, top.h);
            Image small;
            small.w = std::max(1u, uint32_t(std::lround(top.w * f)));
            small.h = std::max(1u, uint32_t(std::lround(top.h * f)));
            small.px.resize(size_t(small.w) * small.h * 4);
            ResizeLanczos(top.px.data(), top.w, top.h, small.px.data(), small.w, small.h);
            if (o.alpha == "1bit")
                for (size_t i = 3; i < small.px.size(); i += 4)
                    small.px[i] = small.px[i] >= 128 ? 255 : 0;
            top = std::move(small);
        }

        // Colour drift against the original: the result shrunk back to the
        // original's size, over the texels the original shows.
        double drift = 0;
        {
            Image back;
            back.w = src.w;
            back.h = src.h;
            back.px.resize(src.px.size());
            ResizeLanczos(top.px.data(), top.w, top.h, back.px.data(), src.w, src.h);
            for (size_t i = 3; i < back.px.size(); i += 4)
                back.px[i] = src.px[i];
            double a[3], b[3];
            MeanRgb(src, a);
            MeanRgb(back, b);
            for (int c = 0; c < 3; c++)
                drift = std::max(drift, std::fabs(a[c] - b[c]));
        }
        if (drift > o.maxDrift)
        {
            char buf[64];
            snprintf(buf, sizeof(buf), "colour drift %.1f/255", drift * 255);
            info = buf;
            return EncodeResult::Drift;
        }

        // The mip chain, and BC7.
        std::vector<Image> levels{ top };
        const double coverage = o.alpha == "1bit" ? Coverage(top, 128) : 0;
        while (levels.back().w > 1 || levels.back().h > 1)
        {
            Image next;
            Box2(levels.back(), next);
            if (o.alpha == "1bit")
                KeepCoverage(next, coverage);
            levels.push_back(std::move(next));
        }
        std::call_once(s_bc7Init, [] { bc7enc_compress_block_init(); });
        bc7enc_compress_block_params params;
        bc7enc_compress_block_params_init(&params);
        params.m_uber_level = 0;

        std::vector<uint8_t> dds(148, 0);
        memcpy(dds.data(), "DDS ", 4);
        Put32(dds, 4, 124);
        Put32(dds, 8, 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x80000);
        Put32(dds, 12, top.h);
        Put32(dds, 16, top.w);
        Put32(dds, 20, ((top.w + 3) / 4) * ((top.h + 3) / 4) * 16);
        Put32(dds, 28, uint32_t(levels.size()));
        Put32(dds, 76, 32);
        Put32(dds, 80, 0x4);
        memcpy(&dds[84], "DX10", 4);
        Put32(dds, 108, 0x1000 | 0x400000 | 0x8);
        Put32(dds, 128, 98);  // DXGI_FORMAT_BC7_UNORM
        Put32(dds, 132, 3);   // 2D
        Put32(dds, 140, 1);   // array size
        for (const Image& l : levels)
        {
            const uint32_t bw = (l.w + 3) / 4, bh = (l.h + 3) / 4;
            const size_t at = dds.size();
            dds.resize(at + size_t(bw) * bh * 16);
            uint8_t pixels[64];
            for (uint32_t by = 0; by < bh; by++)
                for (uint32_t bx = 0; bx < bw; bx++)
                {
                    for (uint32_t y = 0; y < 4; y++)
                        for (uint32_t x = 0; x < 4; x++)
                        {
                            const uint32_t sx = std::min(l.w - 1, bx * 4 + x), sy = std::min(l.h - 1, by * 4 + y);
                            memcpy(&pixels[(y * 4 + x) * 4], &l.px[(size_t(sy) * l.w + sx) * 4], 4);
                        }
                    bc7enc_compress_block(&dds[at + (size_t(by) * bw + bx) * 16], pixels, &params);
                }
        }

        const std::filesystem::path partial = std::filesystem::path(o.out).concat(".partial");
        FILE* f = fopen(partial.c_str(), "wb");
        if (!f)
        {
            info = "can't write " + partial.string();
            return EncodeResult::Error;
        }
        const bool ok = fwrite(dds.data(), 1, dds.size(), f) == dds.size();
        if (fclose(f) != 0 || !ok)
        {
            std::filesystem::remove(partial);
            info = "can't write " + partial.string();
            return EncodeResult::Error;
        }
        std::error_code ec;
        std::filesystem::rename(partial, o.out, ec);
        if (ec)
        {
            info = "can't rename " + partial.string();
            return EncodeResult::Error;
        }
        char buf[96];
        snprintf(buf, sizeof(buf), "%ux%u, %zu levels, drift %.1f/255", top.w, top.h, levels.size(), drift * 255);
        info = buf;
        return EncodeResult::Ok;
    }
}

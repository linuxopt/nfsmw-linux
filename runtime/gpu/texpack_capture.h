// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// Play-capture for the texture pack (docs/superpowers/specs/...-ai-texture-
// pack-design.md §8): the disc extraction can't read every texture (part
// of the city stream uses EA's HUFF compression), so with capture on
// (NFSMW_TEXPACK_CAPTURE=1) the renderer saves each eligible texture once:
// <dir>/<key>.blk, its gathered blocks (texlayout::GatherBase, in guest
// byte order), and <dir>/<key>.json, its Xenos format, endianness and size.
// The pack build reads these beside the disc's textures. Files are written
// on a background thread, each to a .partial first and renamed, so a game
// that quits mid-write never leaves a torn file. Keys already in the folder
// are known at start; capture stops at `capBytes` in all.
#pragma once
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>

namespace gpu::texpack
{
    struct CaptureInfo
    {
        uint32_t format, endian, width, height;
    };

    class Capture
    {
    public:
        // An empty `dir`: capture off.
        Capture(std::filesystem::path dir, uint64_t capBytes);
        ~Capture();
        Capture(const Capture&) = delete;
        Capture& operator=(const Capture&) = delete;

        // A texture of `bytes` gathered bytes is new here and fits the cap.
        bool Want(uint64_t key, size_t bytes) const;
        // Queues it to be written (ignored unless Want).
        void Submit(uint64_t key, const CaptureInfo& info, const std::vector<uint8_t>& blocks);
        // Waits until everything queued is on disk.
        void Flush();
        size_t Count() const { return m_known.size(); }
        uint64_t Bytes() const { return m_bytes; }

    private:
        struct Job
        {
            uint64_t key;
            CaptureInfo info;
            std::vector<uint8_t> blocks;
        };
        void Run();
        void Write(const Job& job) const;

        std::filesystem::path m_dir;
        uint64_t m_cap;
        uint64_t m_bytes = 0;
        std::unordered_set<uint64_t> m_known;
        std::mutex m_mutex;
        std::condition_variable m_wake, m_idle;
        std::deque<Job> m_queue;
        bool m_busy = false, m_stop = false;
        std::thread m_thread;
    };
}

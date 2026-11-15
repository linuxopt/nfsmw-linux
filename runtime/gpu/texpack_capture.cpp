// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
#include "texpack_capture.h"
#include "texpack_key.h"

#include <cstdio>
#include <string>

namespace gpu::texpack
{
    Capture::Capture(std::filesystem::path dir, uint64_t capBytes) : m_dir(std::move(dir)), m_cap(capBytes)
    {
        if (m_dir.empty())
            return;
        std::error_code ec;
        std::filesystem::create_directories(m_dir, ec);
        for (const auto& e : std::filesystem::directory_iterator(m_dir, ec))
        {
            if (e.path().extension() == ".partial")
            {
                std::filesystem::remove(e.path(), ec);  // a write the last run didn't finish
                continue;
            }
            if (e.path().extension() != ".blk")
                continue;
            const std::string stem = e.path().stem().string();
            if (stem.size() != 16 || !std::filesystem::exists(std::filesystem::path(e.path()).replace_extension(".json")))
                continue;
            m_known.insert(std::stoull(stem, nullptr, 16));
            m_bytes += e.file_size(ec);
        }
        m_thread = std::thread([this] { Run(); });
    }

    Capture::~Capture()
    {
        if (!m_thread.joinable())
            return;
        Flush();
        {
            std::lock_guard lock(m_mutex);
            m_stop = true;
        }
        m_wake.notify_one();
        m_thread.join();
    }

    bool Capture::Want(uint64_t key, size_t bytes) const
    {
        return !m_dir.empty() && !m_known.count(key) && m_bytes + bytes <= m_cap;
    }

    void Capture::Submit(uint64_t key, const CaptureInfo& info, const std::vector<uint8_t>& blocks)
    {
        if (!Want(key, blocks.size()))
            return;
        m_known.insert(key);
        m_bytes += blocks.size();
        {
            std::lock_guard lock(m_mutex);
            m_queue.push_back({ key, info, blocks });
        }
        m_wake.notify_one();
    }

    void Capture::Flush()
    {
        std::unique_lock lock(m_mutex);
        m_idle.wait(lock, [this] { return m_queue.empty() && !m_busy; });
    }

    void Capture::Run()
    {
        std::unique_lock lock(m_mutex);
        for (;;)
        {
            m_wake.wait(lock, [this] { return m_stop || !m_queue.empty(); });
            if (m_queue.empty() && m_stop)
                return;
            Job job = std::move(m_queue.front());
            m_queue.pop_front();
            m_busy = true;
            lock.unlock();
            Write(job);
            lock.lock();
            m_busy = false;
            if (m_queue.empty())
                m_idle.notify_all();
        }
    }

    void Capture::Write(const Job& job) const
    {
        const std::string name = KeyName(job.key);
        auto put = [&](const std::string& file, const void* data, size_t size) {
            const std::filesystem::path final = m_dir / file, partial = m_dir / (file + ".partial");
            FILE* f = fopen(partial.c_str(), "wb");
            if (!f)
                return false;
            const bool ok = fwrite(data, 1, size, f) == size;
            if (fclose(f) != 0 || !ok)
            {
                std::error_code ec;
                std::filesystem::remove(partial, ec);
                return false;
            }
            std::error_code ec;
            std::filesystem::rename(partial, final, ec);
            return !ec;
        };
        char json[160];
        const int n = snprintf(json, sizeof(json), "{\"format\": %u, \"endian\": %u, \"width\": %u, \"height\": %u}\n", job.info.format,
            job.info.endian, job.info.width, job.info.height);
        // The blocks first: a .blk without its .json isn't counted at the next start.
        if (put(name + ".blk", job.blocks.data(), job.blocks.size()))
            put(name + ".json", json, size_t(n));
    }
}

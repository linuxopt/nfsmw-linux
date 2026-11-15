// SpeedBreaker desktop launcher. GPL-3.0-or-later (see COPYING). See durable.h.
#include "durable.h"

#include <fcntl.h>
#include <unistd.h>

namespace launcher::durable
{
    namespace
    {
        std::function<void(const std::filesystem::path&)>& Observer()
        {
            static std::function<void(const std::filesystem::path&)> observer;
            return observer;
        }

        bool Sync(const std::filesystem::path& path, int flags)
        {
            if (Observer())
                Observer()(path);
            int fd = ::open(path.c_str(), flags | O_RDONLY | O_CLOEXEC);
            if (fd < 0)
                return false;
            bool ok = ::fsync(fd) == 0;
            ::close(fd);
            return ok;
        }
    }

    bool SyncFile(const std::filesystem::path& file)
    {
        return Sync(file, 0);
    }

    bool SyncDir(const std::filesystem::path& dir)
    {
        return Sync(dir, O_DIRECTORY);
    }

    void SetObserver(std::function<void(const std::filesystem::path&)> observer)
    {
        Observer() = std::move(observer);
    }
}

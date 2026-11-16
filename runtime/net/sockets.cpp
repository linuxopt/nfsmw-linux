// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
#include <stdafx.h>
#include "sockets.h"
#include "online.h"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using HostFd = SOCKET;
constexpr HostFd kNoFd = INVALID_SOCKET;
static void CloseHost(HostFd s) { closesocket(s); }
static int LastHostError() { return WSAGetLastError(); }
static bool InProgress(int e) { return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS; }
static bool WouldBlock(int e) { return e == WSAEWOULDBLOCK; }
static int PollFds(WSAPOLLFD* p, size_t n, int ms) { return WSAPoll(p, ULONG(n), ms); }
using PollFd = WSAPOLLFD;
#else
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
using HostFd = int;
constexpr HostFd kNoFd = -1;
static void CloseHost(HostFd s) { close(s); }
static int LastHostError() { return errno; }
static bool InProgress(int e) { return e == EINPROGRESS || e == EWOULDBLOCK || e == EAGAIN; }
static bool WouldBlock(int e) { return e == EWOULDBLOCK || e == EAGAIN; }
static int PollFds(pollfd* p, size_t n, int ms) { return poll(p, nfds_t(n), ms); }
using PollFd = pollfd;
#endif

namespace net
{
    namespace
    {
        // Winsock errors (the console's values).
        constexpr uint32_t WSAEWOULDBLOCK_ = 10035;
        constexpr uint32_t WSAEINPROGRESS_ = 10036;
        constexpr uint32_t WSAEALREADY_ = 10037;
        constexpr uint32_t WSAENOTSOCK_ = 10038;
        constexpr uint32_t WSAEMSGSIZE_ = 10040;
        constexpr uint32_t WSAEINVAL_ = 10022;
        constexpr uint32_t WSAEADDRINUSE_ = 10048;
        constexpr uint32_t WSAENETDOWN_ = 10050;
        constexpr uint32_t WSAENETUNREACH_ = 10051;
        constexpr uint32_t WSAECONNRESET_ = 10054;
        constexpr uint32_t WSAEISCONN_ = 10056;
        constexpr uint32_t WSAENOTCONN_ = 10057;
        constexpr uint32_t WSAETIMEDOUT_ = 10060;
        constexpr uint32_t WSAECONNREFUSED_ = 10061;
        constexpr uint32_t WSAEOPNOTSUPP_ = 10045;

        constexpr size_t kInboxLimit = 256;   // datagrams queued per socket

        struct Datagram
        {
            Address from;
            std::vector<uint8_t> bytes;
        };

        enum class StreamState { Idle, Connecting, Connected, Failed, Closed };

        struct Socket
        {
            int type = kDatagram;
            bool nonBlocking = false;
            Address local;
            Address peer;
            // Datagrams
            std::deque<Datagram> inbox;
            // Streams
            HostFd fd = kNoFd;
            StreamState state = StreamState::Idle;
            bool peerClosed = false;
            // WSAEventSelect
            uint32_t eventMask = 0;
            std::function<void()> signal;
            uint32_t signalled = 0;   // bits already signalled (re-armed when consumed)
        };

        std::mutex s_mutex;
        std::condition_variable s_changed;
        std::map<int, std::shared_ptr<Socket>> s_sockets;
        int s_nextHandle = 0x5000;
        uint16_t s_nextPort = 50000;
        std::function<bool(uint16_t, std::string&, uint16_t&)> s_serviceTarget;
        bool s_started = false;

        std::shared_ptr<Socket> Find(int handle)
        {
            auto it = s_sockets.find(handle);
            return it == s_sockets.end() ? nullptr : it->second;
        }

        bool PortInUse(uint16_t port, int type)
        {
            for (auto& [h, s] : s_sockets)
                if (s->type == type && s->local.port == port)
                    return true;
            return false;
        }

        uint16_t EphemeralPort(int type)
        {
            for (int i = 0; i < 20000; i++)
            {
                uint16_t p = s_nextPort++;
                if (s_nextPort < 50000)
                    s_nextPort = 50000;
                if (!PortInUse(p, type))
                    return p;
            }
            return 0;
        }

        uint32_t MapHostError(int e)
        {
#ifdef _WIN32
            return uint32_t(e);
#else
            switch (e)
            {
            case ECONNREFUSED: return WSAECONNREFUSED_;
            case ECONNRESET: case EPIPE: return WSAECONNRESET_;
            case ETIMEDOUT: return WSAETIMEDOUT_;
            case ENETUNREACH: case EHOSTUNREACH: return WSAENETUNREACH_;
            case ENOTCONN: return WSAENOTCONN_;
            default: return WSAENETDOWN_;
            }
#endif
        }

        Ready ReadyLocked(Socket& s)
        {
            Ready r;
            if (s.type == kDatagram)
            {
                r.read = !s.inbox.empty();
                r.write = true;
                return r;
            }
            switch (s.state)
            {
            case StreamState::Connected:
            {
                r.write = true;
                PollFd p{};
                p.fd = s.fd;
                p.events = POLLIN;
                if (PollFds(&p, 1, 0) > 0 && (p.revents & (POLLIN | POLLHUP | POLLERR)))
                    r.read = true;
                break;
            }
            case StreamState::Failed:
                r.failed = true;
                r.read = true;
                break;
            case StreamState::Closed:
                r.read = true;
                break;
            default:
                break;
            }
            return r;
        }

        // Signals WSAEventSelect events for what became ready.
        void SignalLocked(Socket& s)
        {
            if (!s.signal || s.eventMask == 0)
                return;
            Ready r = ReadyLocked(s);
            uint32_t now = 0;
            if (r.read && (s.eventMask & FD_READ))
                now |= FD_READ;
            if (r.write && (s.eventMask & FD_WRITE))
                now |= FD_WRITE;
            if (s.type == kStream && (r.write || r.failed) && (s.eventMask & FD_CONNECT))
                now |= FD_CONNECT;
            if (s.peerClosed && (s.eventMask & FD_CLOSE))
                now |= FD_CLOSE;
            uint32_t fresh = now & ~s.signalled;
            s.signalled = now;
            if (fresh)
                s.signal();
        }

        // Checks connecting streams, and signals events, every few ms.
        void WatchLoop()
        {
            for (;;)
            {
                {
                    std::lock_guard lock(s_mutex);
                    for (auto& [h, s] : s_sockets)
                    {
                        if (s->type == kStream && s->state == StreamState::Connecting)
                        {
                            PollFd p{};
                            p.fd = s->fd;
                            p.events = POLLOUT;
                            if (PollFds(&p, 1, 0) > 0)
                            {
                                int err = 0;
                                socklen_t len = sizeof err;
                                getsockopt(s->fd, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len);
                                s->state = err == 0 ? StreamState::Connected : StreamState::Failed;
                                fprintf(stderr, "[net] socket %X: %s\n", h, err == 0 ? "connected" : "connection failed");
                                s_changed.notify_all();
                            }
                        }
                        SignalLocked(*s);
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(4));
            }
        }

        void Deliver(uint32_t from, uint16_t fromPort, uint16_t toPort, const uint8_t* data, size_t length)
        {
            std::lock_guard lock(s_mutex);
            for (auto& [h, s] : s_sockets)
            {
                if (s->type != kDatagram || s->local.port != toPort)
                    continue;
                if (s->inbox.size() >= kInboxLimit)
                    s->inbox.pop_front();
                s->inbox.push_back({ { from, fromPort }, std::vector<uint8_t>(data, data + length) });
                SignalLocked(*s);
                s_changed.notify_all();
                return;
            }
        }

        void StartLocked()
        {
            if (s_started)
                return;
            s_started = true;
            online::SetReceiver(Deliver);
            std::thread(WatchLoop).detach();
        }

        bool SetHostNonBlocking(HostFd fd)
        {
#ifdef _WIN32
            u_long on = 1;
            return ioctlsocket(fd, FIONBIO, &on) == 0;
#else
            int flags = fcntl(fd, F_GETFL, 0);
            return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
        }

        // Waits (unlocking) for `done` or the timeout; true if done.
        template<typename F>
        bool WaitFor(std::unique_lock<std::mutex>& lock, int timeoutMs, F done)
        {
            auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs < 0 ? 1000000000 : timeoutMs);
            while (!done())
            {
                if (std::chrono::steady_clock::now() >= until)
                    return false;
                s_changed.wait_for(lock, std::chrono::milliseconds(4));
            }
            return true;
        }
    }

    void SetServiceTarget(std::function<bool(uint16_t, std::string&, uint16_t&)> target)
    {
        std::lock_guard lock(s_mutex);
        s_serviceTarget = std::move(target);
    }

    int Create(int type, int protocol, uint32_t& error)
    {
        if (type != kStream && type != kDatagram)
        {
            error = WSAEOPNOTSUPP_;
            return -1;
        }
        std::lock_guard lock(s_mutex);
        StartLocked();
        auto s = std::make_shared<Socket>();
        s->type = type;
        int handle = s_nextHandle++;
        s_sockets[handle] = s;
        fprintf(stderr, "[net] socket %X: %s (protocol %d)\n", handle, type == kStream ? "stream" : "datagram", protocol);
        return handle;
    }

    int Close(int handle, uint32_t& error)
    {
        std::lock_guard lock(s_mutex);
        auto s = Find(handle);
        if (!s)
        {
            error = WSAENOTSOCK_;
            return -1;
        }
        if (s->fd != kNoFd)
            CloseHost(s->fd);
        s_sockets.erase(handle);
        s_changed.notify_all();
        return 0;
    }

    bool Exists(int handle)
    {
        std::lock_guard lock(s_mutex);
        return Find(handle) != nullptr;
    }

    int Bind(int handle, Address address, uint32_t& error)
    {
        std::lock_guard lock(s_mutex);
        auto s = Find(handle);
        if (!s)
        {
            error = WSAENOTSOCK_;
            return -1;
        }
        uint16_t port = address.port;
        if (port == 0)
            port = EphemeralPort(s->type);
        else if (PortInUse(port, s->type))
        {
            error = WSAEADDRINUSE_;
            return -1;
        }
        s->local.port = port;
        online::Identity id;
        if (online::GetIdentity(id))
            s->local.ip = id.address;
        return 0;
    }

    int Connect(int handle, Address address, uint32_t& error)
    {
        std::unique_lock lock(s_mutex);
        auto s = Find(handle);
        if (!s)
        {
            error = WSAENOTSOCK_;
            return -1;
        }
        if (s->type == kDatagram)
        {
            s->peer = address;
            if (s->local.port == 0)
                s->local.port = EphemeralPort(kDatagram);
            return 0;
        }
        if (s->state == StreamState::Connecting)
        {
            error = WSAEALREADY_;
            return -1;
        }
        if (s->state == StreamState::Connected)
        {
            error = WSAEISCONN_;
            return -1;
        }
        // Only the services are reachable by stream (players' traffic is
        // datagrams through the relay).
        std::string host;
        uint16_t hostPort = 0;
        if ((address.ip != kServiceAddress && (address.ip >> 24) != 127) || !s_serviceTarget || !s_serviceTarget(address.port, host, hostPort))
        {
            fprintf(stderr, "[net] socket %X: stream to %08X:%u refused (not a service)\n", handle, address.ip, address.port);
            error = WSAECONNREFUSED_;
            return -1;
        }
        addrinfo hints{}, *result = nullptr;
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        std::string portText = std::to_string(hostPort);
        if (getaddrinfo(host.c_str(), portText.c_str(), &hints, &result) != 0 || !result)
        {
            error = WSAENETUNREACH_;
            return -1;
        }
        HostFd fd = socket(result->ai_family, SOCK_STREAM, 0);
        if (fd == kNoFd || !SetHostNonBlocking(fd))
        {
            freeaddrinfo(result);
            if (fd != kNoFd)
                CloseHost(fd);
            error = WSAENETDOWN_;
            return -1;
        }
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof one);
        int rc = connect(fd, result->ai_addr, socklen_t(result->ai_addrlen));
        int e = rc == 0 ? 0 : LastHostError();
        freeaddrinfo(result);
        fprintf(stderr, "[net] socket %X: stream to service port %u -> %s:%u\n", handle, address.port, host.c_str(), hostPort);
        s->fd = fd;
        s->peer = address;
        if (s->local.port == 0)
            s->local.port = EphemeralPort(kStream);
        if (rc == 0)
            s->state = StreamState::Connected;
        else if (InProgress(e))
            s->state = StreamState::Connecting;
        else
        {
            s->state = StreamState::Failed;
            error = MapHostError(e);
            return -1;
        }
        if (s->state == StreamState::Connecting)
        {
            if (s->nonBlocking)
            {
                error = WSAEWOULDBLOCK_;
                return -1;
            }
            WaitFor(lock, 10000, [&] { return s->state != StreamState::Connecting; });
            if (s->state != StreamState::Connected)
            {
                error = s->state == StreamState::Connecting ? WSAETIMEDOUT_ : WSAECONNREFUSED_;
                return -1;
            }
        }
        return 0;
    }

    int Listen(int handle, uint32_t& error)
    {
        error = WSAEOPNOTSUPP_;
        fprintf(stderr, "[net] socket %X: listen is not supported\n", handle);
        return -1;
    }

    int SetNonBlocking(int handle, bool on, uint32_t& error)
    {
        std::lock_guard lock(s_mutex);
        auto s = Find(handle);
        if (!s)
        {
            error = WSAENOTSOCK_;
            return -1;
        }
        s->nonBlocking = on;
        return 0;
    }

    int Shutdown(int handle, uint32_t& error)
    {
        std::lock_guard lock(s_mutex);
        auto s = Find(handle);
        if (!s)
        {
            error = WSAENOTSOCK_;
            return -1;
        }
        if (s->fd != kNoFd)
            shutdown(s->fd, 2);
        return 0;
    }

    int SendTo(int handle, const uint8_t* data, size_t length, Address to, uint32_t& error)
    {
        std::unique_lock lock(s_mutex);
        auto s = Find(handle);
        if (!s)
        {
            error = WSAENOTSOCK_;
            return -1;
        }
        if (s->type == kStream)
        {
            lock.unlock();
            return Send(handle, data, length, error);
        }
        if (length > online::kMaxDatagram)
        {
            error = WSAEMSGSIZE_;
            return -1;
        }
        if (s->local.port == 0)
            s->local.port = EphemeralPort(kDatagram);
        uint16_t fromPort = s->local.port;
        lock.unlock();
        online::Identity id;
        if (online::GetIdentity(id) && to.ip == id.address)
        {
            Deliver(id.address, fromPort, to.port, data, length);   // to itself
            return int(length);
        }
        // A datagram that can't go (offline) is lost, as on a network.
        online::SendDatagram(fromPort, to.ip, to.port, data, length);
        return int(length);
    }

    int Send(int handle, const uint8_t* data, size_t length, uint32_t& error)
    {
        std::unique_lock lock(s_mutex);
        auto s = Find(handle);
        if (!s)
        {
            error = WSAENOTSOCK_;
            return -1;
        }
        if (s->type == kDatagram)
        {
            if (s->peer.ip == 0)
            {
                error = WSAENOTCONN_;
                return -1;
            }
            Address to = s->peer;
            lock.unlock();
            return SendTo(handle, data, length, to, error);
        }
        if (s->state != StreamState::Connected)
        {
            error = s->state == StreamState::Connecting ? WSAEWOULDBLOCK_ : WSAENOTCONN_;
            return -1;
        }
        HostFd fd = s->fd;
        bool nonBlocking = s->nonBlocking;
        lock.unlock();
        size_t sent = 0;
        while (sent < length)
        {
#ifdef MSG_NOSIGNAL
            auto k = send(fd, reinterpret_cast<const char*>(data + sent), int(length - sent), MSG_NOSIGNAL);
#else
            auto k = send(fd, reinterpret_cast<const char*>(data + sent), int(length - sent), 0);
#endif
            if (k > 0)
            {
                sent += size_t(k);
                continue;
            }
            int e = LastHostError();
            if (WouldBlock(e))
            {
                if (nonBlocking)
                    break;
                PollFd p{};
                p.fd = fd;
                p.events = POLLOUT;
                PollFds(&p, 1, 100);
                continue;
            }
            error = MapHostError(e);
            return sent ? int(sent) : -1;
        }
        if (sent == 0 && length > 0)
        {
            error = WSAEWOULDBLOCK_;
            return -1;
        }
        return int(sent);
    }

    int Recv(int handle, uint8_t* data, size_t length, uint32_t& error)
    {
        return RecvFrom(handle, data, length, nullptr, error);
    }

    int RecvFrom(int handle, uint8_t* data, size_t length, Address* from, uint32_t& error)
    {
        std::unique_lock lock(s_mutex);
        auto s = Find(handle);
        if (!s)
        {
            error = WSAENOTSOCK_;
            return -1;
        }
        if (s->type == kDatagram)
        {
            if (s->inbox.empty())
            {
                if (s->nonBlocking)
                {
                    error = WSAEWOULDBLOCK_;
                    return -1;
                }
                WaitFor(lock, -1, [&] { return !s->inbox.empty() || !s_sockets.count(handle); });
                if (s->inbox.empty())
                {
                    error = WSAENOTSOCK_;
                    return -1;
                }
            }
            Datagram d = std::move(s->inbox.front());
            s->inbox.pop_front();
            s->signalled &= ~FD_READ;
            size_t n = std::min(length, d.bytes.size());
            memcpy(data, d.bytes.data(), n);
            if (from)
                *from = d.from;
            if (d.bytes.size() > length)
            {
                error = WSAEMSGSIZE_;
                return -1;
            }
            return int(n);
        }
        if (s->state != StreamState::Connected)
        {
            if (s->state == StreamState::Connecting && s->nonBlocking)
            {
                error = WSAEWOULDBLOCK_;
                return -1;
            }
            if (s->state == StreamState::Closed)
                return 0;
            error = WSAENOTCONN_;
            return -1;
        }
        HostFd fd = s->fd;
        bool nonBlocking = s->nonBlocking;
        if (from)
            *from = s->peer;
        lock.unlock();
        for (;;)
        {
            auto k = recv(fd, reinterpret_cast<char*>(data), int(length), 0);
            if (k > 0)
            {
                std::lock_guard relock(s_mutex);
                s->signalled &= ~FD_READ;
                return int(k);
            }
            if (k == 0)
            {
                std::lock_guard relock(s_mutex);
                s->peerClosed = true;
                s->state = StreamState::Closed;
                return 0;
            }
            int e = LastHostError();
            if (!WouldBlock(e))
            {
                error = MapHostError(e);
                return -1;
            }
            if (nonBlocking)
            {
                std::lock_guard relock(s_mutex);
                s->signalled &= ~FD_READ;
                error = WSAEWOULDBLOCK_;
                return -1;
            }
            PollFd p{};
            p.fd = fd;
            p.events = POLLIN;
            PollFds(&p, 1, 100);
        }
    }

    bool LocalAddress(int handle, Address& out)
    {
        std::lock_guard lock(s_mutex);
        auto s = Find(handle);
        if (!s)
            return false;
        out = s->local;
        online::Identity id;
        if (out.ip == 0 && online::GetIdentity(id))
            out.ip = id.address;
        return true;
    }

    bool PeerAddress(int handle, Address& out)
    {
        std::lock_guard lock(s_mutex);
        auto s = Find(handle);
        if (!s || (s->type == kStream && s->state != StreamState::Connected) || s->peer.ip == 0)
            return false;
        out = s->peer;
        return true;
    }

    int Available(int handle, uint32_t& error)
    {
        std::lock_guard lock(s_mutex);
        auto s = Find(handle);
        if (!s)
        {
            error = WSAENOTSOCK_;
            return -1;
        }
        if (s->type == kDatagram)
            return s->inbox.empty() ? 0 : int(s->inbox.front().bytes.size());
        if (s->state != StreamState::Connected)
            return 0;
#ifdef _WIN32
        u_long n = 0;
        ioctlsocket(s->fd, FIONREAD, &n);
#else
        int n = 0;
        ioctl(s->fd, FIONREAD, &n);
#endif
        return int(n);
    }

    Ready Poll(int handle)
    {
        std::lock_guard lock(s_mutex);
        auto s = Find(handle);
        if (!s)
            return {};
        return ReadyLocked(*s);
    }

    void Select(const std::vector<int>& read, const std::vector<int>& write, int timeoutMs,
        std::vector<int>& readReady, std::vector<int>& writeReady)
    {
        std::unique_lock lock(s_mutex);
        auto check = [&] {
            readReady.clear();
            writeReady.clear();
            for (int h : read)
                if (auto s = Find(h); s && ReadyLocked(*s).read)
                    readReady.push_back(h);
            for (int h : write)
                if (auto s = Find(h); s && (ReadyLocked(*s).write || ReadyLocked(*s).failed))
                    writeReady.push_back(h);
            return !readReady.empty() || !writeReady.empty();
        };
        WaitFor(lock, timeoutMs, check);
    }

    int EventSelect(int handle, uint32_t mask, std::function<void()> signal, uint32_t& error)
    {
        std::lock_guard lock(s_mutex);
        auto s = Find(handle);
        if (!s)
        {
            error = WSAENOTSOCK_;
            return -1;
        }
        s->eventMask = mask;
        s->signal = std::move(signal);
        s->signalled = 0;
        s->nonBlocking = true;   // as Winsock does
        SignalLocked(*s);
        return 0;
    }
}

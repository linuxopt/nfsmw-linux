// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// The console's network library (NetDll_*, XNet*) for online races through
// the SpeedBreaker online server (scripts/online/nfsmw_server.py): every
// player gets an address from the server, and the game's packets to other
// players go through the server's relay (net/online.h), so nobody needs an
// open port. Behaviour follows Xenia's netplay fork (xam_net.cc,
// BSD-3-Clause; see THIRD_PARTY_NOTICES.md) where the game relies on it.
#include <stdafx.h>
#include "dispatcher.h"
#include "function.h"
#include "imports_net.h"
#include <cpu/guest_thread.h>
#include <net/online.h>
#include <net/sockets.h>
#include <region/region.h>

#include <list>
#include <thread>

using namespace dispatcher;

uint32_t NtClose(uint32_t handle);
uint32_t ExAllocatePoolWithTag(uint32_t size, uint32_t tag);
void ExFreePool(uint32_t address);

namespace
{
    constexpr uint32_t STATUS_SUCCESS_ = 0;

    // XNetGetTitleXnAddr status bits.
    constexpr uint32_t XNADDR_PENDING = 0x00;
    constexpr uint32_t XNADDR_ETHERNET = 0x02;
    constexpr uint32_t XNADDR_STATIC = 0x04;
    constexpr uint32_t XNADDR_GATEWAY = 0x20;
    constexpr uint32_t XNADDR_DNS = 0x40;
    constexpr uint32_t XNADDR_ONLINE = 0x80;
    constexpr uint32_t XNADDR_TROUBLESHOOT = 0x8000;

    template<typename T>
    T* FromHandle(uint32_t handle)
    {
        if (handle == GUEST_INVALID_HANDLE_VALUE || !IsKernelObject(handle))
            return nullptr;
        return dynamic_cast<T*>(GetKernelObject(handle));
    }
}

uint32_t NetDll_XNetStartup(uint32_t caller, void* params)
{
    online::Start();
    return 0;
}

uint32_t NetDll_XNetCleanup(uint32_t caller)
{
    return 0;
}

uint32_t NetDll_WSAStartup(uint32_t caller, uint32_t version, X_WSADATA* data)
{
    online::Start();
    if (data)
    {
        memset(data, 0, sizeof(*data));
        data->version = uint16_t(version);
        data->highVersion = 0x0202;
    }
    return 0;
}

uint32_t NetDll_WSACleanup(uint32_t caller)
{
    return 0;
}

uint32_t NetDll_WSAGetLastError()
{
    return GuestThread::GetLastError();
}

void NetDll_WSASetLastError(uint32_t error)
{
    GuestThread::SetLastError(error);
}

// ---- Events (plain kernel events) ------------------------------------------------

uint32_t NetDll_WSACreateEvent()
{
    return GetKernelHandle(CreateKernelObject<EventObject>(true, false));  // manual reset, not signalled
}

uint32_t NetDll_WSACloseEvent(uint32_t handle)
{
    auto* e = FromHandle<EventObject>(handle);
    if (e == nullptr)
    {
        GuestThread::SetLastError(X_WSAEINVAL);
        return 0;
    }
    NtClose(handle);
    return 1;
}

uint32_t NetDll_WSAResetEvent(uint32_t handle)
{
    auto* e = FromHandle<EventObject>(handle);
    if (e == nullptr)
    {
        GuestThread::SetLastError(X_WSAEINVAL);
        return 0;
    }
    e->Reset();
    return 1;
}

uint32_t NetDll_WSASetEvent(uint32_t handle)
{
    auto* e = FromHandle<EventObject>(handle);
    if (e == nullptr)
    {
        GuestThread::SetLastError(X_WSAEINVAL);
        return 0;
    }
    e->Set();
    return 1;
}

uint32_t NetDll_WSAWaitForMultipleEvents(uint32_t count, be<uint32_t>* events, uint32_t waitAll, uint32_t timeoutMs,
    uint32_t alertable)
{
    if (count == 0 || count > 64)
    {
        GuestThread::SetLastError(X_WSAEINVAL);
        return 0xFFFFFFFF;
    }
    std::vector<Waitable*> objects;
    for (uint32_t i = 0; i < count; i++)
    {
        Waitable* w = WaitableFromHandle(events[i]);
        if (w == nullptr)
        {
            GuestThread::SetLastError(6);  // ERROR_INVALID_HANDLE
            return 0xFFFFFFFF;
        }
        objects.push_back(w);
    }
    be<int64_t> timeout = -10000ll * int64_t(timeoutMs);
    // WAIT_OBJECT_0 + i, WAIT_TIMEOUT (0x102): the same values as the kernel wait's.
    return WaitForObjects(objects, waitAll != 0, Deadline(timeoutMs == 0xFFFFFFFF ? nullptr : &timeout));
}

// ---- Addresses --------------------------------------------------------------------

uint32_t NetDll_XNetGetTitleXnAddr(uint32_t caller, XNADDR* addr)
{
    memset(addr, 0, sizeof(*addr));
    online::Identity id;
    if (!online::GetIdentity(id))
    {
        // Not connected (yet): the game asks again until it isn't pending.
        if (online::WaitConnected(50) == online::Connection::Connecting)
            return XNADDR_PENDING;
        return XNADDR_ETHERNET | XNADDR_TROUBLESHOOT;
    }
    online::FillXnAddr(id.address, *addr);
    return XNADDR_ETHERNET | XNADDR_STATIC | XNADDR_GATEWAY | XNADDR_DNS | XNADDR_ONLINE;
}

uint32_t NetDll_XNetGetEthernetLinkStatus(uint32_t caller)
{
    return 0x01 | 0x02 | 0x08;  // XNET_ETHERNET_LINK_ACTIVE | 100MBPS | FULL_DUPLEX
}

uint32_t NetDll_XNetRandom(uint32_t caller, uint8_t* buffer, uint32_t length)
{
    online::Random(buffer, length);
    return 0;
}

// ---- Sockets ------------------------------------------------------------------------

namespace
{
    constexpr uint32_t SOCKET_ERROR_ = 0xFFFFFFFF;
    constexpr uint32_t WSA_IO_PENDING_ = 997;
    constexpr uint32_t WSA_IO_INCOMPLETE_ = 996;

    struct XSOCKADDR_IN
    {
        be<uint16_t> family;
        be<uint16_t> port;
        be<uint32_t> address;
        uint8_t zero[8];
    };

    struct X_FD_SET
    {
        be<uint32_t> count;
        be<uint32_t> handles[64];
    };

    struct X_TIMEVAL
    {
        be<int32_t> seconds;
        be<int32_t> microseconds;
    };

    struct X_WSABUF
    {
        be<uint32_t> length;
        be<uint32_t> buffer;
    };

    struct X_WSAOVERLAPPED
    {
        be<uint32_t> internal;       // status
        be<uint32_t> internalHigh;   // bytes
        be<uint32_t> offset;
        be<uint32_t> offsetHigh;
        be<uint32_t> event;
    };

    net::Address FromGuest(const XSOCKADDR_IN* a)
    {
        return a ? net::Address{ uint32_t(a->address), uint16_t(a->port) } : net::Address{};
    }

    void ToGuest(XSOCKADDR_IN* a, be<uint32_t>* length, net::Address address)
    {
        if (!a)
            return;
        memset(a, 0, sizeof(*a));
        a->family = 2;  // AF_INET
        a->port = address.port;
        a->address = address.ip;
        if (length)
            *length = sizeof(XSOCKADDR_IN);
    }

    uint32_t Fail(uint32_t error)
    {
        GuestThread::SetLastError(error);
        return SOCKET_ERROR_;
    }

    uint32_t Result(int rc, uint32_t error)
    {
        return rc < 0 ? Fail(error) : uint32_t(rc);
    }

    void SignalGuestEvent(uint32_t handle)
    {
        if (auto* e = FromHandle<EventObject>(handle))
            e->Set();
    }

    // Overlapped receives that had nothing to read: finished by the pump
    // thread when data comes.
    struct PendingRecv
    {
        int socket;
        std::vector<std::pair<uint32_t, uint32_t>> buffers;  // guest address, length
        uint32_t overlapped;
        uint32_t from;        // XSOCKADDR_IN*, or 0
        uint32_t fromLength;  // be<uint32_t>*, or 0
    };
    std::mutex s_pendingMutex;
    std::list<PendingRecv> s_pendingRecvs;

    // One receive into guest buffers; >= 0 bytes, or -1 with `error`.
    int ReceiveInto(int socket, const std::vector<std::pair<uint32_t, uint32_t>>& buffers, net::Address* from,
        uint32_t& error)
    {
        size_t total = 0;
        for (auto& b : buffers)
            total += b.second;
        std::vector<uint8_t> data(std::max<size_t>(total, 1));
        int n = net::RecvFrom(socket, data.data(), total, from, error);
        if (n <= 0)
            return n;
        size_t at = 0;
        for (auto& [address, length] : buffers)
        {
            size_t k = std::min<size_t>(length, size_t(n) - at);
            memcpy(g_memory.Translate(address), data.data() + at, k);
            at += k;
            if (at >= size_t(n))
                break;
        }
        return n;
    }

    void PumpLoop()
    {
        for (;;)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            std::lock_guard lock(s_pendingMutex);
            for (auto it = s_pendingRecvs.begin(); it != s_pendingRecvs.end();)
            {
                auto* ov = static_cast<X_WSAOVERLAPPED*>(g_memory.Translate(it->overlapped));
                if (!net::Exists(it->socket))
                {
                    ov->internal = 995;  // ERROR_OPERATION_ABORTED
                    ov->internalHigh = 0;
                    SignalGuestEvent(ov->event);
                    it = s_pendingRecvs.erase(it);
                    continue;
                }
                if (!net::Poll(it->socket).read)
                {
                    ++it;
                    continue;
                }
                uint32_t error = 0;
                net::Address from;
                int n = ReceiveInto(it->socket, it->buffers, &from, error);
                if (n < 0 && error == X_WSAEWOULDBLOCK)
                {
                    ++it;
                    continue;
                }
                if (n >= 0 && it->from)
                    ToGuest(static_cast<XSOCKADDR_IN*>(g_memory.Translate(it->from)),
                        it->fromLength ? static_cast<be<uint32_t>*>(g_memory.Translate(it->fromLength)) : nullptr, from);
                ov->internalHigh = n < 0 ? 0 : uint32_t(n);
                ov->internal = n < 0 ? error : 0;
                SignalGuestEvent(ov->event);
                it = s_pendingRecvs.erase(it);
            }
        }
    }

    void QueueRecv(PendingRecv recv)
    {
        static std::once_flag started;
        std::call_once(started, [] { std::thread(PumpLoop).detach(); });
        std::lock_guard lock(s_pendingMutex);
        s_pendingRecvs.push_back(std::move(recv));
    }

    std::vector<std::pair<uint32_t, uint32_t>> GuestBuffers(const X_WSABUF* buffers, uint32_t count)
    {
        std::vector<std::pair<uint32_t, uint32_t>> out;
        for (uint32_t i = 0; buffers && i < count; i++)
            out.emplace_back(uint32_t(buffers[i].buffer), uint32_t(buffers[i].length));
        return out;
    }

    std::vector<uint8_t> Gather(const X_WSABUF* buffers, uint32_t count)
    {
        std::vector<uint8_t> out;
        for (uint32_t i = 0; buffers && i < count; i++)
        {
            auto* p = static_cast<uint8_t*>(g_memory.Translate(buffers[i].buffer));
            out.insert(out.end(), p, p + uint32_t(buffers[i].length));
        }
        return out;
    }

    std::vector<int> Handles(const X_FD_SET* set)
    {
        std::vector<int> out;
        if (set)
            for (uint32_t i = 0; i < std::min<uint32_t>(set->count, 64); i++)
                out.push_back(int(uint32_t(set->handles[i])));
        return out;
    }

    void WriteSet(X_FD_SET* set, const std::vector<int>& handles)
    {
        if (!set)
            return;
        set->count = uint32_t(handles.size());
        for (size_t i = 0; i < handles.size() && i < 64; i++)
            set->handles[i] = uint32_t(handles[i]);
    }
}

namespace
{
    // DirtySock's address map ('xmap', SocketControl sub_8282A998 keeps the
    // table's address at 0x829118D8; sub_828298E0 applies it to every
    // address the game sends to). Online, NetConn installs one entry
    // {address 0, mask 0 -> the service address}, which matches everything:
    // every race packet would go to EA's service instead of the other
    // player. As the NFSMW Recompiled project found, only the game's own
    // loopback names (127.x, its 'xdns') are meant to go there, so the
    // entry is narrowed to 127.0.0.0/8 before the game sends anything.
    // Only an entry that maps to our service address is touched.
    constexpr uint32_t kXmapTable = 0x829118D8;

    void NarrowXmap()
    {
        uint32_t table = *static_cast<be<uint32_t>*>(g_memory.Translate(region::Addr(kXmapTable)));
        if (table == 0)
            return;
        auto* entry = static_cast<be<uint32_t>*>(g_memory.Translate(table));
        for (int i = 0; i < 16 && entry[2] != 0; i++, entry += 3)
        {
            if (entry[1] == 0 && entry[2] == net::kServiceAddress)
            {
                entry[0] = 0x7F000000;
                entry[1] = 0xFF000000;
                fprintf(stderr, "[net] address map: only 127.x goes to the service now\n");
            }
        }
    }
}

uint32_t NetDll_socket(uint32_t caller, uint32_t family, uint32_t type, uint32_t protocol)
{
    uint32_t error = 0;
    int h = net::Create(int(type), int(protocol), error);
    return h < 0 ? Fail(error) : uint32_t(h);
}

uint32_t NetDll_closesocket(uint32_t caller, uint32_t socket)
{
    uint32_t error = 0;
    return Result(net::Close(int(socket), error), error);
}

uint32_t NetDll_shutdown(uint32_t caller, uint32_t socket, uint32_t how)
{
    uint32_t error = 0;
    return Result(net::Shutdown(int(socket), error), error);
}

uint32_t NetDll_bind(uint32_t caller, uint32_t socket, XSOCKADDR_IN* name, uint32_t length)
{
    uint32_t error = 0;
    return Result(net::Bind(int(socket), FromGuest(name), error), error);
}

uint32_t NetDll_connect(uint32_t caller, uint32_t socket, XSOCKADDR_IN* name, uint32_t length)
{
    NarrowXmap();
    uint32_t error = 0;
    return Result(net::Connect(int(socket), FromGuest(name), error), error);
}

uint32_t NetDll_listen(uint32_t caller, uint32_t socket, uint32_t backlog)
{
    uint32_t error = 0;
    return Result(net::Listen(int(socket), error), error);
}

uint32_t NetDll_accept(uint32_t caller, uint32_t socket, XSOCKADDR_IN* address, be<uint32_t>* length)
{
    return Fail(10045);  // WSAEOPNOTSUPP: nobody connects to a player by stream
}

uint32_t NetDll_send(uint32_t caller, uint32_t socket, uint8_t* data, uint32_t length, uint32_t flags)
{
    uint32_t error = 0;
    return Result(net::Send(int(socket), data, length, error), error);
}

uint32_t NetDll_sendto(uint32_t caller, uint32_t socket, uint8_t* data, uint32_t length, uint32_t flags,
    XSOCKADDR_IN* to, uint32_t toLength)
{
    NarrowXmap();
    uint32_t error = 0;
    if (!to)
        return Result(net::Send(int(socket), data, length, error), error);
    return Result(net::SendTo(int(socket), data, length, FromGuest(to), error), error);
}

uint32_t NetDll_recv(uint32_t caller, uint32_t socket, uint8_t* data, uint32_t length, uint32_t flags)
{
    uint32_t error = 0;
    return Result(net::Recv(int(socket), data, length, error), error);
}

uint32_t NetDll_recvfrom(uint32_t caller, uint32_t socket, uint8_t* data, uint32_t length, uint32_t flags,
    XSOCKADDR_IN* from, be<uint32_t>* fromLength)
{
    uint32_t error = 0;
    net::Address address;
    int n = net::RecvFrom(int(socket), data, length, &address, error);
    if (n >= 0)
        ToGuest(from, fromLength, address);
    return Result(n, error);
}

uint32_t NetDll_select(uint32_t caller, uint32_t nfds, X_FD_SET* readSet, X_FD_SET* writeSet, X_FD_SET* exceptSet,
    X_TIMEVAL* timeout)
{
    if (!readSet && !writeSet && !exceptSet)
        return Fail(X_WSAEINVAL);
    std::vector<int> read = Handles(readSet), write = writeSet != readSet ? Handles(writeSet) : std::vector<int>{};
    for (int h : read)
        if (!net::Exists(h))
            return Fail(X_WSAENOTSOCK);
    int ms = timeout ? int(timeout->seconds) * 1000 + int(timeout->microseconds) / 1000 : -1;
    std::vector<int> readReady, writeReady;
    net::Select(read, write, ms, readReady, writeReady);
    WriteSet(readSet, readReady);
    if (writeSet && writeSet != readSet)
        WriteSet(writeSet, writeReady);
    if (exceptSet && exceptSet != readSet && exceptSet != writeSet)
        exceptSet->count = 0;
    return uint32_t(readReady.size() + writeReady.size());
}

uint32_t NetDll___WSAFDIsSet(uint32_t socket, X_FD_SET* set)
{
    for (int h : Handles(set))
        if (uint32_t(h) == socket)
            return 1;
    return 0;
}

uint32_t NetDll_ioctlsocket(uint32_t caller, uint32_t socket, uint32_t command, be<uint32_t>* argument)
{
    uint32_t error = 0;
    switch (command)
    {
    case 0x8004667E:  // FIONBIO
        return Result(net::SetNonBlocking(int(socket), argument && *argument != 0, error), error);
    case 0x4004667F:  // FIONREAD
    {
        int n = net::Available(int(socket), error);
        if (n >= 0 && argument)
            *argument = uint32_t(n);
        return Result(n < 0 ? -1 : 0, error);
    }
    }
    fprintf(stderr, "[net] ioctlsocket %08X ignored\n", command);
    return net::Exists(int(socket)) ? 0 : Fail(X_WSAENOTSOCK);
}

uint32_t NetDll_setsockopt(uint32_t caller, uint32_t socket, uint32_t level, uint32_t name, void* value, uint32_t length)
{
    // Buffer sizes, broadcast, the VDP and linger options: nothing to do.
    return net::Exists(int(socket)) ? 0 : Fail(X_WSAENOTSOCK);
}

uint32_t NetDll_getsockopt(uint32_t caller, uint32_t socket, uint32_t level, uint32_t name, be<uint32_t>* value,
    be<uint32_t>* length)
{
    if (!net::Exists(int(socket)))
        return Fail(X_WSAENOTSOCK);
    if (value && length && *length >= 4)
    {
        uint32_t v = 0;
        if (level == 0xFFFF && name == 0x1007)  // SOL_SOCKET, SO_ERROR
            v = net::Poll(int(socket)).failed ? 10061 : 0;
        else if (level == 0xFFFF && (name == 0x1001 || name == 0x1002))  // SO_SNDBUF, SO_RCVBUF
            v = 65536;
        *value = v;
        *length = 4;
    }
    return 0;
}

uint32_t NetDll_getsockname(uint32_t caller, uint32_t socket, XSOCKADDR_IN* address, be<uint32_t>* length)
{
    net::Address a;
    if (!net::LocalAddress(int(socket), a))
        return Fail(X_WSAENOTSOCK);
    ToGuest(address, length, a);
    return 0;
}

uint32_t NetDll_getpeername(uint32_t caller, uint32_t socket, XSOCKADDR_IN* address, be<uint32_t>* length)
{
    net::Address a;
    if (!net::PeerAddress(int(socket), a))
        return Fail(net::Exists(int(socket)) ? X_WSAENOTCONN : X_WSAENOTSOCK);
    ToGuest(address, length, a);
    return 0;
}

uint32_t NetDll_inet_addr(const char* text)
{
    unsigned a, b, c, d;
    char extra;
    if (!text || sscanf(text, "%u.%u.%u.%u%c", &a, &b, &c, &d, &extra) != 4 || a > 255 || b > 255 || c > 255 || d > 255)
        return 0xFFFFFFFF;  // INADDR_NONE
    return a << 24 | b << 16 | c << 8 | d;
}

uint32_t NetDll_WSAEventSelect(uint32_t caller, uint32_t socket, uint32_t event, uint32_t mask)
{
    uint32_t error = 0;
    std::function<void()> signal;
    if (event && mask)
        signal = [event] { SignalGuestEvent(event); };
    return Result(net::EventSelect(int(socket), mask, std::move(signal), error), error);
}

uint32_t NetDll_WSARecvFrom(uint32_t caller, uint32_t socket, X_WSABUF* buffers, uint32_t count, be<uint32_t>* received,
    be<uint32_t>* flags, XSOCKADDR_IN* from, be<uint32_t>* fromLength, X_WSAOVERLAPPED* overlapped, uint32_t routine)
{
    if (!net::Exists(int(socket)))
        return Fail(X_WSAENOTSOCK);
    if (flags)
        *flags = 0;
    auto list = GuestBuffers(buffers, count);
    uint32_t error = 0;
    net::Address address;
    if (overlapped && !net::Poll(int(socket)).read)
    {
        overlapped->internal = WSA_IO_PENDING_;
        overlapped->internalHigh = 0;
        QueueRecv({ int(socket), list, g_memory.MapVirtual(overlapped), from ? g_memory.MapVirtual(from) : 0u,
            fromLength ? g_memory.MapVirtual(fromLength) : 0u });
        return Fail(WSA_IO_PENDING_);
    }
    int n = ReceiveInto(int(socket), list, &address, error);
    if (n < 0)
        return Fail(error);
    ToGuest(from, fromLength, address);
    if (received)
        *received = uint32_t(n);
    if (overlapped)
    {
        overlapped->internal = 0;
        overlapped->internalHigh = uint32_t(n);
        SignalGuestEvent(overlapped->event);
    }
    return 0;
}

uint32_t NetDll_WSARecv(uint32_t caller, uint32_t socket, X_WSABUF* buffers, uint32_t count, be<uint32_t>* received,
    be<uint32_t>* flags, X_WSAOVERLAPPED* overlapped, uint32_t routine)
{
    return NetDll_WSARecvFrom(caller, socket, buffers, count, received, flags, nullptr, nullptr, overlapped, routine);
}

uint32_t NetDll_WSASendTo(uint32_t caller, uint32_t socket, X_WSABUF* buffers, uint32_t count, be<uint32_t>* sent,
    uint32_t flags, XSOCKADDR_IN* to, uint32_t toLength, X_WSAOVERLAPPED* overlapped, uint32_t routine)
{
    NarrowXmap();
    std::vector<uint8_t> data = Gather(buffers, count);
    uint32_t error = 0;
    int n = to ? net::SendTo(int(socket), data.data(), data.size(), FromGuest(to), error)
               : net::Send(int(socket), data.data(), data.size(), error);
    if (n < 0)
        return Fail(error);
    if (sent)
        *sent = uint32_t(n);
    if (overlapped)
    {
        overlapped->internal = 0;
        overlapped->internalHigh = uint32_t(n);
        SignalGuestEvent(overlapped->event);
    }
    return 0;
}

uint32_t NetDll_WSASend(uint32_t caller, uint32_t socket, X_WSABUF* buffers, uint32_t count, be<uint32_t>* sent,
    uint32_t flags, X_WSAOVERLAPPED* overlapped, uint32_t routine)
{
    return NetDll_WSASendTo(caller, socket, buffers, count, sent, flags, nullptr, 0, overlapped, routine);
}

uint32_t NetDll_WSAGetOverlappedResult(uint32_t caller, uint32_t socket, X_WSAOVERLAPPED* overlapped,
    be<uint32_t>* bytes, uint32_t wait, be<uint32_t>* flags)
{
    if (!overlapped)
        return Fail(X_WSAEINVAL);
    if (overlapped->internal == WSA_IO_PENDING_)
    {
        if (!wait)
        {
            GuestThread::SetLastError(WSA_IO_INCOMPLETE_);
            return 0;
        }
        while (overlapped->internal == WSA_IO_PENDING_)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (bytes)
        *bytes = overlapped->internalHigh;
    if (flags)
        *flags = 0;
    if (overlapped->internal != 0)
    {
        GuestThread::SetLastError(overlapped->internal);
        return 0;
    }
    return 1;
}

uint32_t NetDll_WSACancelOverlappedIO(uint32_t caller, uint32_t socket, X_WSAOVERLAPPED* overlapped, uint32_t unknown)
{
    std::lock_guard lock(s_pendingMutex);
    uint32_t address = overlapped ? g_memory.MapVirtual(overlapped) : 0;
    for (auto it = s_pendingRecvs.begin(); it != s_pendingRecvs.end(); ++it)
    {
        if (it->socket == int(socket) && (address == 0 || it->overlapped == address))
        {
            auto* ov = static_cast<X_WSAOVERLAPPED*>(g_memory.Translate(it->overlapped));
            ov->internal = 995;  // ERROR_OPERATION_ABORTED
            SignalGuestEvent(ov->event);
            s_pendingRecvs.erase(it);
            break;
        }
    }
    return 0;
}

// ---- XNet addresses, keys and connection quality ----------------------------------------

namespace
{
    struct XNKID { uint8_t ab[8]; };
    struct XNKEY { uint8_t ab[16]; };

    struct XNQOSINFO
    {
        uint8_t flags;
        uint8_t reserved;
        be<uint16_t> probesSent;
        be<uint16_t> probesReceived;
        be<uint16_t> dataLength;
        be<uint32_t> data;
        be<uint16_t> rttMin;
        be<uint16_t> rttMedian;
        be<uint32_t> upBitsPerSecond;
        be<uint32_t> downBitsPerSecond;
    };
    static_assert(sizeof(XNQOSINFO) == 24);
}

uint32_t NetDll_XNetXnAddrToInAddr(uint32_t caller, XNADDR* addr, XNKID* kid, be<uint32_t>* ina)
{
    if (!addr || !ina)
        return X_WSAEINVAL;
    *ina = online::AddressFromXnAddr(*addr);
    return 0;
}

uint32_t NetDll_XNetInAddrToXnAddr(uint32_t caller, uint32_t ina, XNADDR* addr, XNKID* kid)
{
    if (addr)
        online::FillXnAddr(ina, *addr);
    if (kid)
        memset(kid, 0, sizeof(*kid));
    return 0;
}

uint32_t NetDll_XNetXnAddrToMachineId(uint32_t caller, XNADDR* addr, be<uint64_t>* machineId)
{
    if (!addr || !machineId)
        return X_WSAEINVAL;
    *machineId = 0xFA00000000000000ull | uint32_t(addr->ina);
    return 0;
}

uint32_t NetDll_XNetServerToInAddr(uint32_t caller, uint32_t ina, uint32_t serviceId, be<uint32_t>* out)
{
    if (out)
        *out = ina;
    return 0;
}

uint32_t NetDll_XNetInAddrToServer(uint32_t caller, uint32_t ina, be<uint32_t>* out)
{
    if (out)
        *out = ina;
    return 0;
}

uint32_t NetDll_XNetTsAddrToInAddr(uint32_t caller, uint32_t tsAddr, uint32_t serviceId, void* key, be<uint32_t>* out)
{
    if (out)
        *out = tsAddr;
    return 0;
}

uint32_t NetDll_XNetInAddrToString(uint32_t caller, uint32_t ina, char* text, uint32_t size)
{
    if (text && size)
        snprintf(text, size, "%u.%u.%u.%u", ina >> 24, (ina >> 16) & 0xFF, (ina >> 8) & 0xFF, ina & 0xFF);
    return 0;
}

uint32_t NetDll_XNetConnect(uint32_t caller, uint32_t ina) { return 0; }
uint32_t NetDll_XNetGetConnectStatus(uint32_t caller, uint32_t ina) { return 2; }  // XNET_CONNECT_STATUS_CONNECTED
uint32_t NetDll_XNetUnregisterInAddr(uint32_t caller, uint32_t ina) { return 0; }
uint32_t NetDll_XNetRegisterKey(uint32_t caller, XNKID* kid, XNKEY* key) { return 0; }
uint32_t NetDll_XNetUnregisterKey(uint32_t caller, XNKID* kid) { return 0; }

uint32_t NetDll_XNetCreateKey(uint32_t caller, XNKID* kid, XNKEY* key)
{
    if (kid)
    {
        online::Random(kid->ab, 8);
        kid->ab[0] = (kid->ab[0] & 0x0F) | 0x90;  // an online peer session, open NAT
    }
    if (key)
        online::Random(key->ab, 16);
    return 0;
}

uint32_t NetDll_XNetGetOpt(uint32_t caller, uint32_t option, be<uint32_t>* value, be<uint32_t>* length)
{
    if (value && length && *length >= 4)
    {
        *value = 65536;  // buffer sizes
        *length = 4;
    }
    return 0;
}

uint32_t NetDll_XNetSetOpt(uint32_t caller, uint32_t option, void* value, uint32_t length) { return 0; }

uint32_t NetDll_XNetGetBroadcastVersionStatus(uint32_t caller, uint32_t reset) { return 0; }

uint32_t NetDll_XNetGetDebugXnAddr(uint32_t caller, XNADDR* addr)
{
    return NetDll_XNetGetTitleXnAddr(caller, addr);
}

uint32_t NetDll_XNetDnsLookup(uint32_t caller, const char* host, uint32_t event, be<uint32_t>* out)
{
    // EA's host names all mean the online server (the service address).
    fprintf(stderr, "[net] XNetDnsLookup \"%s\" -> the online server\n", host ? host : "");
    uint32_t dns = ExAllocatePoolWithTag(0x28, 0);
    auto* p = static_cast<be<uint32_t>*>(g_memory.Translate(dns));
    memset(p, 0, 0x28);
    p[0] = 0;   // status
    p[1] = 1;   // cina
    p[2] = net::kServiceAddress;
    if (out)
        *out = dns;
    SignalGuestEvent(event);
    return 0;
}

uint32_t NetDll_XNetDnsRelease(uint32_t caller, uint32_t dns)
{
    ExFreePool(dns);
    return 0;
}

// XNetQosListen: what this player answers QoS probes with (games put
// lobby details in it); kept by the server for other players' lookups.
uint32_t NetDll_XNetQosListen(uint32_t caller, XNKID* kid, uint8_t* data, uint32_t length, uint32_t bitsPerSecond,
    uint32_t flags)
{
    if ((flags & 0x04) && data)  // XNET_QOS_LISTEN_SET_DATA
    {
        online::Writer w;
        w.Bytes(data, std::min<uint32_t>(length, 1024));
        std::vector<uint8_t> reply;
        online::Call(online::op::QosListen, w.bytes, reply);
    }
    if (flags & 0x12)  // DISABLE, RELEASE
    {
        online::Writer w;
        w.Bytes(nullptr, 0);
        std::vector<uint8_t> reply;
        online::Call(online::op::QosListen, w.bytes, reply);
    }
    return 0;
}

// XNetQosLookup: results for every player asked about at once (the server
// has their listen data), with the round trip to the server as the ping.
uint32_t NetDll_XNetQosLookup(uint32_t caller, uint32_t count, be<uint32_t>* addresses, be<uint32_t>* kids,
    be<uint32_t>* keys, uint32_t gatewayCount, be<uint32_t>* gateways, be<uint32_t>* serviceIds, uint32_t probes,
    uint32_t bitsPerSecond, uint32_t flags, uint32_t event, be<uint32_t>* out)
{
    if (!out)
        return 10013;  // WSAEACCES
    uint32_t total = count + gatewayCount;
    uint32_t size = 8 + std::max<uint32_t>(total, 1) * sizeof(XNQOSINFO);
    std::vector<std::vector<uint8_t>> datas(count);
    std::vector<bool> found(count);
    auto start = std::chrono::steady_clock::now();
    if (count)
    {
        online::Writer w;
        w.U32(count);
        for (uint32_t i = 0; i < count; i++)
            w.U32(online::AddressFromXnAddr(*static_cast<XNADDR*>(g_memory.Translate(addresses[i]))));
        std::vector<uint8_t> reply;
        if (online::Call(online::op::QosLookup, w.bytes, reply))
        {
            online::Reader r(reply);
            uint32_t n = r.U32();
            for (uint32_t i = 0; i < n && i < count && r.ok; i++)
            {
                found[i] = r.U8() != 0;
                datas[i] = r.Bytes(1024);
            }
        }
    }
    uint16_t rtt = uint16_t(std::clamp<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count(), 1, 1000));
    for (auto& d : datas)
        size += uint32_t(d.size());
    uint32_t qos = ExAllocatePoolWithTag(size, 0);
    auto* base = static_cast<uint8_t*>(g_memory.Translate(qos));
    memset(base, 0, size);
    *reinterpret_cast<be<uint32_t>*>(base) = total;
    *reinterpret_cast<be<uint32_t>*>(base + 4) = 0;
    auto* infos = reinterpret_cast<XNQOSINFO*>(base + 8);
    uint32_t dataAt = 8 + std::max<uint32_t>(total, 1) * sizeof(XNQOSINFO);
    for (uint32_t i = 0; i < total; i++)
    {
        XNQOSINFO& q = infos[i];
        bool reached = i >= count || found[i];
        q.flags = 0x01 | (reached ? 0x02 : 0x04);  // COMPLETE, TARGET_CONTACTED or TARGET_DISABLED
        q.probesSent = uint16_t(probes ? probes : 4);
        q.probesReceived = reached ? uint16_t(probes ? probes : 4) : 0;
        q.rttMin = rtt;
        q.rttMedian = rtt;
        q.upBitsPerSecond = 2000000;
        q.downBitsPerSecond = 2000000;
        if (i < count && reached && !datas[i].empty())
        {
            q.flags |= 0x08;  // DATA_RECEIVED
            q.dataLength = uint16_t(datas[i].size());
            q.data = qos + dataAt;
            memcpy(base + dataAt, datas[i].data(), datas[i].size());
            dataAt += uint32_t(datas[i].size());
        }
    }
    *out = qos;
    SignalGuestEvent(event);
    return 0;
}

uint32_t NetDll_XNetQosServiceLookup(uint32_t caller, uint32_t flags, uint32_t event, be<uint32_t>* out)
{
    uint32_t qos = ExAllocatePoolWithTag(8 + sizeof(XNQOSINFO), 0);
    auto* base = static_cast<uint8_t*>(g_memory.Translate(qos));
    memset(base, 0, 8 + sizeof(XNQOSINFO));
    *reinterpret_cast<be<uint32_t>*>(base) = 1;
    auto* q = reinterpret_cast<XNQOSINFO*>(base + 8);
    q->flags = 0x03;
    q->probesSent = 4;
    q->probesReceived = 4;
    q->rttMin = 30;
    q->rttMedian = 30;
    q->upBitsPerSecond = 2000000;
    q->downBitsPerSecond = 2000000;
    if (out)
        *out = qos;
    SignalGuestEvent(event);
    return 0;
}

uint32_t NetDll_XNetQosRelease(uint32_t caller, uint32_t qos)
{
    ExFreePool(qos);
    return 0;
}

uint32_t NetDll_XNetQosGetListenStats(uint32_t caller, XNKID* kid, void* stats)
{
    return 0;
}

GUEST_FUNCTION_HOOK(__imp__NetDll_socket, NetDll_socket);
GUEST_FUNCTION_HOOK(__imp__NetDll_closesocket, NetDll_closesocket);
GUEST_FUNCTION_HOOK(__imp__NetDll_shutdown, NetDll_shutdown);
GUEST_FUNCTION_HOOK(__imp__NetDll_bind, NetDll_bind);
GUEST_FUNCTION_HOOK(__imp__NetDll_connect, NetDll_connect);
GUEST_FUNCTION_HOOK(__imp__NetDll_listen, NetDll_listen);
GUEST_FUNCTION_HOOK(__imp__NetDll_accept, NetDll_accept);
GUEST_FUNCTION_HOOK(__imp__NetDll_send, NetDll_send);
GUEST_FUNCTION_HOOK(__imp__NetDll_sendto, NetDll_sendto);
GUEST_FUNCTION_HOOK(__imp__NetDll_recv, NetDll_recv);
GUEST_FUNCTION_HOOK(__imp__NetDll_recvfrom, NetDll_recvfrom);
GUEST_FUNCTION_HOOK(__imp__NetDll_select, NetDll_select);
GUEST_FUNCTION_HOOK(__imp__NetDll___WSAFDIsSet, NetDll___WSAFDIsSet);
GUEST_FUNCTION_HOOK(__imp__NetDll_ioctlsocket, NetDll_ioctlsocket);
GUEST_FUNCTION_HOOK(__imp__NetDll_setsockopt, NetDll_setsockopt);
GUEST_FUNCTION_HOOK(__imp__NetDll_getsockopt, NetDll_getsockopt);
GUEST_FUNCTION_HOOK(__imp__NetDll_getsockname, NetDll_getsockname);
GUEST_FUNCTION_HOOK(__imp__NetDll_getpeername, NetDll_getpeername);
GUEST_FUNCTION_HOOK(__imp__NetDll_inet_addr, NetDll_inet_addr);
GUEST_FUNCTION_HOOK(__imp__NetDll_WSAEventSelect, NetDll_WSAEventSelect);
GUEST_FUNCTION_HOOK(__imp__NetDll_WSARecvFrom, NetDll_WSARecvFrom);
GUEST_FUNCTION_HOOK(__imp__NetDll_WSARecv, NetDll_WSARecv);
GUEST_FUNCTION_HOOK(__imp__NetDll_WSASendTo, NetDll_WSASendTo);
GUEST_FUNCTION_HOOK(__imp__NetDll_WSASend, NetDll_WSASend);
GUEST_FUNCTION_HOOK(__imp__NetDll_WSAGetOverlappedResult, NetDll_WSAGetOverlappedResult);
GUEST_FUNCTION_HOOK(__imp__NetDll_WSACancelOverlappedIO, NetDll_WSACancelOverlappedIO);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetXnAddrToInAddr, NetDll_XNetXnAddrToInAddr);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetInAddrToXnAddr, NetDll_XNetInAddrToXnAddr);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetXnAddrToMachineId, NetDll_XNetXnAddrToMachineId);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetServerToInAddr, NetDll_XNetServerToInAddr);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetInAddrToServer, NetDll_XNetInAddrToServer);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetTsAddrToInAddr, NetDll_XNetTsAddrToInAddr);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetInAddrToString, NetDll_XNetInAddrToString);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetConnect, NetDll_XNetConnect);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetGetConnectStatus, NetDll_XNetGetConnectStatus);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetUnregisterInAddr, NetDll_XNetUnregisterInAddr);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetRegisterKey, NetDll_XNetRegisterKey);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetUnregisterKey, NetDll_XNetUnregisterKey);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetCreateKey, NetDll_XNetCreateKey);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetGetOpt, NetDll_XNetGetOpt);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetSetOpt, NetDll_XNetSetOpt);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetGetBroadcastVersionStatus, NetDll_XNetGetBroadcastVersionStatus);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetGetDebugXnAddr, NetDll_XNetGetDebugXnAddr);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetDnsLookup, NetDll_XNetDnsLookup);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetDnsRelease, NetDll_XNetDnsRelease);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetQosListen, NetDll_XNetQosListen);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetQosLookup, NetDll_XNetQosLookup);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetQosServiceLookup, NetDll_XNetQosServiceLookup);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetQosRelease, NetDll_XNetQosRelease);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetQosGetListenStats, NetDll_XNetQosGetListenStats);

// ---- Logon ------------------------------------------------------------------------

uint32_t XNetLogonGetTitleID(uint32_t caller, void* params)
{
    return 0x454107D9;  // Need for Speed: Most Wanted (every release)
}

uint32_t XNetLogonGetMachineID(be<uint64_t>* machineId)
{
    online::Identity id;
    if (!online::GetIdentity(id))
    {
        if (machineId)
            *machineId = 0;
        return 0x80151000;  // XONLINE_E_LOGON_NOT_LOGGED_ON
    }
    if (machineId)
        *machineId = 0xFA00000000000000ull | id.address;
    return 0;
}

GUEST_FUNCTION_HOOK(__imp__XNetLogonGetTitleID, XNetLogonGetTitleID);
GUEST_FUNCTION_HOOK(__imp__XNetLogonGetMachineID, XNetLogonGetMachineID);

// ---- XAM's own allocator (used by the online library) -----------------------------

uint32_t XamAlloc(uint32_t flags, uint32_t size, be<uint32_t>* out)
{
    uint32_t address = ExAllocatePoolWithTag(size, 0);
    if (address == 0)
        return 0x8007000E;  // E_OUTOFMEMORY
    memset(g_memory.Translate(address), 0, size);
    *out = address;
    return 0;
}

void XamFree(uint32_t address)
{
    if (address)
        ExFreePool(address);
}

GUEST_FUNCTION_HOOK(__imp__NetDll_XNetStartup, NetDll_XNetStartup);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetCleanup, NetDll_XNetCleanup);
GUEST_FUNCTION_HOOK(__imp__NetDll_WSAStartup, NetDll_WSAStartup);
GUEST_FUNCTION_HOOK(__imp__NetDll_WSACleanup, NetDll_WSACleanup);
GUEST_FUNCTION_HOOK(__imp__NetDll_WSAGetLastError, NetDll_WSAGetLastError);
GUEST_FUNCTION_HOOK(__imp__NetDll_WSASetLastError, NetDll_WSASetLastError);
GUEST_FUNCTION_HOOK(__imp__NetDll_WSACreateEvent, NetDll_WSACreateEvent);
GUEST_FUNCTION_HOOK(__imp__NetDll_WSACloseEvent, NetDll_WSACloseEvent);
GUEST_FUNCTION_HOOK(__imp__NetDll_WSAResetEvent, NetDll_WSAResetEvent);
GUEST_FUNCTION_HOOK(__imp__NetDll_WSASetEvent, NetDll_WSASetEvent);
GUEST_FUNCTION_HOOK(__imp__NetDll_WSAWaitForMultipleEvents, NetDll_WSAWaitForMultipleEvents);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetGetTitleXnAddr, NetDll_XNetGetTitleXnAddr);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetGetEthernetLinkStatus, NetDll_XNetGetEthernetLinkStatus);
GUEST_FUNCTION_HOOK(__imp__NetDll_XNetRandom, NetDll_XNetRandom);
GUEST_FUNCTION_HOOK(__imp__XamAlloc, XamAlloc);
GUEST_FUNCTION_HOOK(__imp__XamFree, XamFree);

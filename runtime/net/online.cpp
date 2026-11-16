// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// The connection to the online server (online.h): one TCP connection for
// requests, one UDP socket for the relay, a thread for each.
#include <stdafx.h>
#include "online.h"

#include <kernel/imports_net.h>
#include "sockets.h"
#include <ui/ui.h>
#include <user/paths.h>

#if __has_include(<toml++/toml.hpp>)
#include <toml++/toml.hpp>
#else
#include "../../tools/XenonRecomp/thirdparty/tomlplusplus/include/toml++/toml.hpp"
#endif

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <fstream>
#include <map>
#include <mutex>
#include <random>
#include <sstream>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using SocketFd = SOCKET;
constexpr SocketFd kNoSocket = INVALID_SOCKET;
static void CloseFd(SocketFd s) { closesocket(s); }
#else
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
using SocketFd = int;
constexpr SocketFd kNoSocket = -1;
static void CloseFd(SocketFd s) { close(s); }
#endif

namespace online
{
    namespace
    {
        constexpr uint32_t kProtocol = 1;
        constexpr uint8_t kUdpKeepalive = 1;
        constexpr uint8_t kUdpData = 2;

        struct Config
        {
            std::string server;
            std::string name;
            uint64_t xuid = 0;
        };

        std::mutex s_mutex;  // everything below
        std::once_flag s_started;
        std::once_flag s_configLoaded;
        Config s_config;
        Connection s_state = Connection::Off;
        std::string s_failure;
        Identity s_identity;
        uint32_t s_token = 0;
        SocketFd s_tcp = kNoSocket;
        SocketFd s_udp = kNoSocket;
        Receiver s_receiver;
        uint32_t s_nextRequest = 1;

        struct Pending
        {
            bool done = false;
            uint8_t op = 0;
            std::vector<uint8_t> body;
        };
        std::map<uint32_t, Pending*> s_pending;
        std::condition_variable s_replied;
        std::mutex s_sendMutex;  // whole TCP frames

        std::filesystem::path ConfigPath() { return GetUserPath() / "online.toml"; }

        std::string RandomName()
        {
            std::random_device rd;
            return "Racer" + std::to_string(1000 + rd() % 9000);
        }

        // Names other players see: 1-15 printable ASCII characters.
        std::string CleanName(std::string name)
        {
            std::string out;
            for (char c : name)
                if (c >= 0x20 && c < 0x7F && out.size() < 15)
                    out += c;
            while (!out.empty() && out.back() == ' ')
                out.pop_back();
            return out;
        }

        void SaveConfig(const Config& c)
        {
            std::ostringstream text;
            text << "# SpeedBreaker online play. server: host:port of the online server;\n"
                 << "# name: what other players see (up to 15 characters). Keep id as it is:\n"
                 << "# it is how the server tells you apart.\n";
            toml::table t;
            t.insert("enabled", true);
            t.insert("server", c.server);
            t.insert("name", c.name);
            char id[17];
            snprintf(id, sizeof id, "%016llX", (unsigned long long)c.xuid);
            t.insert("id", std::string(id));
            text << t << "\n";
            std::filesystem::path path = ConfigPath(), tmp = path;
            tmp += ".partial";
            {
                std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
                f << text.str();
                if (!f)
                    return;
            }
            std::error_code ec;
            std::filesystem::rename(tmp, path, ec);
        }

        void LoadConfig()
        {
            // No online.toml: online play is off (the launcher's Online page
            // writes one with kDefaultServer) unless NFSMW_ONLINE_SERVER names
            // a server, which then makes one.
            Config c;
            std::filesystem::path path = ConfigPath();
            const char* envServer = std::getenv("NFSMW_ONLINE_SERVER");
            if (!std::filesystem::exists(path) && !(envServer && *envServer))
            {
                s_config = c;
                return;
            }
            c.server = kDefaultServer;
            bool write = true;
            if (std::filesystem::exists(path))
            {
                write = false;
                try
                {
                    toml::table t = toml::parse_file(path.string());
                    if (!t["enabled"].value_or(true) && !(envServer && *envServer))
                    {
                        s_config = Config{};   // turned off (the launcher's Online page)
                        return;
                    }
                    if (auto v = t["server"].value<std::string>())
                        c.server = *v;
                    if (auto v = t["name"].value<std::string>())
                        c.name = CleanName(*v);
                    if (auto v = t["id"].value<std::string>())
                        c.xuid = strtoull(v->c_str(), nullptr, 16);
                }
                catch (const std::exception& e)
                {
                    fprintf(stderr, "[online] %s: %s (online play is off)\n", path.string().c_str(), e.what());
                    c.server.clear();
                }
            }
            if (c.name.empty())
            {
                c.name = RandomName();
                write = true;
            }
            // Ids made here look like Xbox Live online XUIDs (0x0009...), which
            // the game's peer code expects, and never match the offline
            // profile's (0xB13E...).
            if ((c.xuid >> 48) != 0x0009)
            {
                std::random_device rd;
                c.xuid = 0x0009000000000000ull | ((uint64_t(rd()) << 32 | rd()) & 0x0000FFFFFFFFFFFFull);
                write = true;
            }
            if (write)
                SaveConfig(c);
            if (envServer && *envServer)
                c.server = envServer;
            if (const char* v = std::getenv("NFSMW_ONLINE_NAME"); v && *v)
                c.name = CleanName(v);
            s_config = c;
        }

        void EnsureConfig() { std::call_once(s_configLoaded, LoadConfig); }

        void Fail(const std::string& reason)
        {
            {
                std::lock_guard lock(s_mutex);
                if (s_state == Connection::Failed)
                    return;
                s_state = Connection::Failed;
                s_failure = reason;
                for (auto& [id, p] : s_pending)
                {
                    p->done = true;
                    p->op = op::Error;
                }
            }
            s_replied.notify_all();
            fprintf(stderr, "[online] offline: %s\n", reason.c_str());
            ui::Toast("Online: " + reason);
        }

        bool SendAll(SocketFd s, const uint8_t* p, size_t n)
        {
            while (n > 0)
            {
#ifdef MSG_NOSIGNAL
                auto k = send(s, reinterpret_cast<const char*>(p), int(n), MSG_NOSIGNAL);
#else
                auto k = send(s, reinterpret_cast<const char*>(p), int(n), 0);
#endif
                if (k <= 0)
                    return false;
                p += k;
                n -= size_t(k);
            }
            return true;
        }

        bool RecvAll(SocketFd s, uint8_t* p, size_t n)
        {
            while (n > 0)
            {
                auto k = recv(s, reinterpret_cast<char*>(p), int(n), 0);
                if (k <= 0)
                    return false;
                p += k;
                n -= size_t(k);
            }
            return true;
        }

        bool SendFrame(uint8_t opcode, uint32_t request, const std::vector<uint8_t>& body)
        {
            Writer w;
            w.U32(uint32_t(5 + body.size()));
            w.U8(opcode);
            w.U32(request);
            w.Raw(body.data(), body.size());
            std::lock_guard lock(s_sendMutex);
            return SendAll(s_tcp, w.bytes.data(), w.bytes.size());
        }

        bool Resolve(const std::string& server, int type, sockaddr_storage& out, socklen_t& length)
        {
            size_t colon = server.rfind(':');
            if (colon == std::string::npos)
                return false;
            std::string host = server.substr(0, colon), port = server.substr(colon + 1);
            if (host.size() > 2 && host.front() == '[' && host.back() == ']')
                host = host.substr(1, host.size() - 2);
            addrinfo hints{}, *result = nullptr;
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = type;
            if (getaddrinfo(host.c_str(), port.c_str(), &hints, &result) != 0 || result == nullptr)
                return false;
            memcpy(&out, result->ai_addr, result->ai_addrlen);
            length = socklen_t(result->ai_addrlen);
            freeaddrinfo(result);
            return true;
        }

        void UdpLoop()
        {
            uint8_t buffer[2048];
            auto lastKeepalive = std::chrono::steady_clock::now() - std::chrono::seconds(10);
            for (;;)
            {
                SocketFd udp;
                uint32_t token;
                {
                    std::lock_guard lock(s_mutex);
                    if (s_state != Connection::Online)
                        return;
                    udp = s_udp;
                    token = s_token;
                }
                auto now = std::chrono::steady_clock::now();
                if (now - lastKeepalive > std::chrono::seconds(4))
                {
                    Writer w;
                    w.U8(kUdpKeepalive);
                    w.U32(token);
                    send(udp, reinterpret_cast<const char*>(w.bytes.data()), int(w.bytes.size()), 0);
                    lastKeepalive = now;
                }
#ifdef _WIN32
                WSAPOLLFD pfd{ udp, POLLIN, 0 };
                int ready = WSAPoll(&pfd, 1, 500);
#else
                pollfd pfd{ udp, POLLIN, 0 };
                int ready = poll(&pfd, 1, 500);
#endif
                if (ready <= 0)
                    continue;
                auto k = recv(udp, reinterpret_cast<char*>(buffer), sizeof buffer, 0);
                // [kind][u32 from address][u16 from port][u16 to port][payload]
                if (k < 9 || buffer[0] != kUdpData)
                    continue;
                uint32_t from = uint32_t(buffer[1]) << 24 | uint32_t(buffer[2]) << 16 | uint32_t(buffer[3]) << 8 | buffer[4];
                uint16_t fromPort = uint16_t(buffer[5] << 8 | buffer[6]);
                uint16_t toPort = uint16_t(buffer[7] << 8 | buffer[8]);
                Receiver receiver;
                {
                    std::lock_guard lock(s_mutex);
                    receiver = s_receiver;
                }
                if (receiver)
                    receiver(from, fromPort, toPort, buffer + 9, size_t(k) - 9);
            }
        }

        void TcpLoop()
        {
            for (;;)
            {
                uint8_t header[9];
                if (!RecvAll(s_tcp, header, 4))
                    return Fail("the connection to the server was lost");
                uint32_t length = uint32_t(header[0]) << 24 | uint32_t(header[1]) << 16 | uint32_t(header[2]) << 8 | header[3];
                if (length < 5 || length > (4u << 20))
                    return Fail("the server sent a bad message");
                std::vector<uint8_t> frame(length);
                if (!RecvAll(s_tcp, frame.data(), length))
                    return Fail("the connection to the server was lost");
                uint8_t opcode = frame[0];
                uint32_t request = uint32_t(frame[1]) << 24 | uint32_t(frame[2]) << 16 | uint32_t(frame[3]) << 8 | frame[4];
                {
                    std::lock_guard lock(s_mutex);
                    auto it = s_pending.find(request);
                    if (it != s_pending.end())
                    {
                        it->second->done = true;
                        it->second->op = opcode;
                        it->second->body.assign(frame.begin() + 5, frame.end());
                    }
                }
                s_replied.notify_all();
            }
        }

        void Connect()
        {
            Config c;
            {
                std::lock_guard lock(s_mutex);
                c = s_config;
            }
            fprintf(stderr, "[online] connecting to %s as \"%s\"\n", c.server.c_str(), c.name.c_str());
            sockaddr_storage address{};
            socklen_t length = 0;
            if (!Resolve(c.server, SOCK_STREAM, address, length))
                return Fail("can't find the server " + c.server);
            SocketFd tcp = socket(address.ss_family, SOCK_STREAM, 0);
            if (tcp == kNoSocket || connect(tcp, reinterpret_cast<sockaddr*>(&address), length) != 0)
            {
                if (tcp != kNoSocket)
                    CloseFd(tcp);
                return Fail("can't reach the server " + c.server);
            }
            int one = 1;
            setsockopt(tcp, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof one);
            s_tcp = tcp;

            Writer hello;
            hello.U32(kProtocol);
            hello.Str(c.name);
            hello.U64(c.xuid);
            if (!SendFrame(op::Hello, 0, hello.bytes))
                return Fail("can't reach the server " + c.server);
            uint8_t header[4];
            if (!RecvAll(tcp, header, 4))
                return Fail("the server closed the connection");
            uint32_t frameLength = uint32_t(header[0]) << 24 | uint32_t(header[1]) << 16 | uint32_t(header[2]) << 8 | header[3];
            if (frameLength < 5 || frameLength > 65536)
                return Fail("the server sent a bad reply");
            std::vector<uint8_t> frame(frameLength);
            if (!RecvAll(tcp, frame.data(), frameLength))
                return Fail("the server closed the connection");
            std::vector<uint8_t> body(frame.begin() + 5, frame.end());
            Reader r(body);
            if (frame[0] == op::Error)
                return Fail(r.Str());
            uint32_t playerAddress = r.U32();
            uint32_t token = r.U32();
            std::string motd = r.Str();
            if (!r.ok || frame[0] != op::Hello)
                return Fail("the server sent a bad reply");

            sockaddr_storage udpAddress{};
            socklen_t udpLength = 0;
            SocketFd udp = kNoSocket;
            if (Resolve(c.server, SOCK_DGRAM, udpAddress, udpLength))
                udp = socket(udpAddress.ss_family, SOCK_DGRAM, 0);
            if (udp == kNoSocket || connect(udp, reinterpret_cast<sockaddr*>(&udpAddress), udpLength) != 0)
                return Fail("can't open the relay");
            {
                std::lock_guard lock(s_mutex);
                s_udp = udp;
                s_token = token;
                s_identity.address = playerAddress;
                s_identity.xuid = c.xuid;
                s_identity.name = c.name;
                s_state = Connection::Online;
            }
            fprintf(stderr, "[online] online as \"%s\" (%u.%u.%u.%u)%s%s\n", c.name.c_str(), playerAddress >> 24,
                (playerAddress >> 16) & 0xFF, (playerAddress >> 8) & 0xFF, playerAddress & 0xFF, motd.empty() ? "" : ": ",
                motd.c_str());
            if (!motd.empty())
                ui::Toast(motd);
            std::thread(UdpLoop).detach();
            TcpLoop();
        }
    }

    void Start()
    {
        std::call_once(s_started, [] {
            EnsureConfig();
            {
                std::lock_guard lock(s_mutex);
                if (s_config.server.empty())
                {
                    s_state = Connection::Off;
                    return;
                }
                s_state = Connection::Connecting;
            }
#ifdef _WIN32
            WSADATA data;
            WSAStartup(MAKEWORD(2, 2), &data);
#endif
            // Streams to a service go to the server's next port.
            net::SetServiceTarget([](uint16_t port, std::string& host, uint16_t& hostPort) {
                uint16_t serverPort = 0;
                if (!ServerEndpoint(host, serverPort))
                    return false;
                hostPort = uint16_t(serverPort + 1);
                return true;
            });
            std::thread(Connect).detach();
        });
    }

    bool Enabled()
    {
        EnsureConfig();
        std::lock_guard lock(s_mutex);
        return !s_config.server.empty();
    }

    Connection State()
    {
        std::lock_guard lock(s_mutex);
        return s_state;
    }

    Connection WaitConnected(int timeoutMs)
    {
        auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        for (;;)
        {
            Connection c = State();
            if (c != Connection::Connecting || std::chrono::steady_clock::now() >= until)
                return c;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }

    std::string FailureReason()
    {
        std::lock_guard lock(s_mutex);
        return s_failure;
    }

    bool GetIdentity(Identity& out)
    {
        std::lock_guard lock(s_mutex);
        if (s_state != Connection::Online)
            return false;
        out = s_identity;
        return true;
    }

    std::string PlayerName()
    {
        EnsureConfig();
        std::lock_guard lock(s_mutex);
        return s_config.name;
    }

    uint64_t PlayerXuid()
    {
        EnsureConfig();
        std::lock_guard lock(s_mutex);
        return s_config.xuid;
    }

    bool ServerEndpoint(std::string& host, uint16_t& port)
    {
        EnsureConfig();
        std::lock_guard lock(s_mutex);
        size_t colon = s_config.server.rfind(':');
        if (colon == std::string::npos)
            return false;
        host = s_config.server.substr(0, colon);
        if (host.size() > 2 && host.front() == '[' && host.back() == ']')
            host = host.substr(1, host.size() - 2);
        port = uint16_t(atoi(s_config.server.c_str() + colon + 1));
        return port != 0;
    }

    void FillXnAddr(uint32_t address, XNADDR& out)
    {
        memset(&out, 0, sizeof out);
        out.ina = address;
        out.inaOnline = address;
        out.wPortOnline = 1000;
        // A locally administered MAC and an online id made from the address.
        const uint8_t mac[6] = { 0x02, 0x53, 0x42, uint8_t(address >> 16), uint8_t(address >> 8), uint8_t(address) };
        memcpy(out.abEnet, mac, 6);
        for (int i = 0; i < 20; i += 4)
        {
            out.abOnline[i] = uint8_t(address >> 24);
            out.abOnline[i + 1] = uint8_t(address >> 16);
            out.abOnline[i + 2] = uint8_t(address >> 8);
            out.abOnline[i + 3] = uint8_t(address);
        }
    }

    uint32_t AddressFromXnAddr(const XNADDR& addr)
    {
        return addr.ina;
    }

    void Random(uint8_t* buffer, size_t length)
    {
        static std::mutex m;
        static std::mt19937_64 rng(std::random_device{}());
        std::lock_guard lock(m);
        for (size_t i = 0; i < length; i++)
            buffer[i] = uint8_t(rng());
    }

    bool Call(uint8_t opcode, const std::vector<uint8_t>& body, std::vector<uint8_t>& reply, int timeoutMs)
    {
        reply.clear();
        Pending pending;
        uint32_t request;
        {
            std::lock_guard lock(s_mutex);
            if (s_state != Connection::Online)
                return false;
            request = s_nextRequest++;
            s_pending[request] = &pending;
        }
        bool sent = SendFrame(opcode, request, body);
        std::unique_lock lock(s_mutex);
        if (sent)
            s_replied.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] { return pending.done; });
        s_pending.erase(request);
        if (!pending.done || pending.op == op::Error)
        {
            if (pending.done)
            {
                Reader r(pending.body);
                fprintf(stderr, "[online] request %u refused: %s\n", opcode, r.Str().c_str());
            }
            else
                fprintf(stderr, "[online] request %u: no reply\n", opcode);
            return false;
        }
        reply = std::move(pending.body);
        return true;
    }

    void SetReceiver(Receiver receiver)
    {
        std::lock_guard lock(s_mutex);
        s_receiver = std::move(receiver);
    }

    bool SendDatagram(uint16_t fromPort, uint32_t toAddress, uint16_t toPort, const uint8_t* data, size_t length)
    {
        if (length > kMaxDatagram)
            return false;
        SocketFd udp;
        uint32_t token;
        {
            std::lock_guard lock(s_mutex);
            if (s_state != Connection::Online)
                return false;
            udp = s_udp;
            token = s_token;
        }
        uint8_t packet[13 + kMaxDatagram];
        packet[0] = kUdpData;
        packet[1] = uint8_t(token >> 24); packet[2] = uint8_t(token >> 16); packet[3] = uint8_t(token >> 8); packet[4] = uint8_t(token);
        packet[5] = uint8_t(fromPort >> 8); packet[6] = uint8_t(fromPort);
        packet[7] = uint8_t(toAddress >> 24); packet[8] = uint8_t(toAddress >> 16); packet[9] = uint8_t(toAddress >> 8); packet[10] = uint8_t(toAddress);
        packet[11] = uint8_t(toPort >> 8); packet[12] = uint8_t(toPort);
        memcpy(packet + 13, data, length);
        return send(udp, reinterpret_cast<const char*>(packet), int(13 + length), 0) == int(13 + length);
    }
}

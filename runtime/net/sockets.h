// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// The game's sockets (kernel/imports_net.cpp is the guest side):
//   - datagram sockets (UDP, and the console's VDP, which the game uses for
//     race traffic) live here: what the game sends to another player goes
//     through the online server's relay (online.h), and what other players
//     send to this one is queued on the socket bound to its port;
//   - stream sockets (TCP) are host sockets; a connection to a service
//     address (MapService) goes to the online server's service port.
// Addresses and ports are host order. Each call returns >= 0, or -1 with
// the Winsock error in `error`.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace net
{
    constexpr int kStream = 1;
    constexpr int kDatagram = 2;

    // WSAEventSelect bits.
    constexpr uint32_t FD_READ = 0x01;
    constexpr uint32_t FD_WRITE = 0x02;
    constexpr uint32_t FD_ACCEPT = 0x08;
    constexpr uint32_t FD_CONNECT = 0x10;
    constexpr uint32_t FD_CLOSE = 0x20;

    struct Address
    {
        uint32_t ip = 0;
        uint16_t port = 0;
    };

    // Readiness (select, WSAEventSelect).
    struct Ready
    {
        bool read = false;     // data, a connection to accept, or closed
        bool write = false;    // connected and room to send
        bool failed = false;   // a connect that failed
    };

    int Create(int type, int protocol, uint32_t& error);
    int Close(int handle, uint32_t& error);
    bool Exists(int handle);
    int Bind(int handle, Address address, uint32_t& error);
    int Connect(int handle, Address address, uint32_t& error);
    int Listen(int handle, uint32_t& error);
    int SetNonBlocking(int handle, bool on, uint32_t& error);
    int Shutdown(int handle, uint32_t& error);
    int Send(int handle, const uint8_t* data, size_t length, uint32_t& error);
    int SendTo(int handle, const uint8_t* data, size_t length, Address to, uint32_t& error);
    int Recv(int handle, uint8_t* data, size_t length, uint32_t& error);
    // `from` gets the sender (datagrams) or the peer (streams).
    int RecvFrom(int handle, uint8_t* data, size_t length, Address* from, uint32_t& error);
    bool LocalAddress(int handle, Address& out);
    bool PeerAddress(int handle, Address& out);
    // Bytes waiting (FIONREAD).
    int Available(int handle, uint32_t& error);

    Ready Poll(int handle);
    // Waits until any handle in the lists is ready or `timeoutMs` passes
    // (-1: no limit); returns the ready ones.
    void Select(const std::vector<int>& read, const std::vector<int>& write, int timeoutMs,
        std::vector<int>& readReady, std::vector<int>& writeReady);

    // WSAEventSelect: `signal` is called (on any thread) whenever the socket
    // becomes ready for something in `mask`; empty to stop.
    int EventSelect(int handle, uint32_t mask, std::function<void()> signal, uint32_t& error);

    // The address the game is given for a service (XOnlineGetServiceInfo),
    // and where a stream connected to it really goes.
    constexpr uint32_t kServiceAddress = 0x0A500001;   // 10.80.0.1
    void SetServiceTarget(std::function<bool(uint16_t port, std::string& host, uint16_t& hostPort)> target);
}

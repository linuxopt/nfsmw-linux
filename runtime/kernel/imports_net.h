// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// Guest structures of the console's network library (imports_net.cpp).
#pragma once
#include <cstdint>
#include <cpu/ppc_context.h>
#include <xbox.h>

// WSAStartup's WSADATA (only the versions are filled).
struct X_WSADATA
{
    be<uint16_t> version;
    be<uint16_t> highVersion;
    char description[257];
    char systemStatus[129];
    be<uint16_t> maxSockets;
    be<uint16_t> maxUdpDg;
    be<uint32_t> vendorInfo;
};

// A player's network address as the game sees it (36 bytes).
struct XNADDR
{
    be<uint32_t> ina;        // address
    be<uint32_t> inaOnline;  // public address
    be<uint16_t> wPortOnline;
    uint8_t abEnet[6];       // MAC address
    uint8_t abOnline[20];    // online id
};
static_assert(sizeof(XNADDR) == 36);

// Winsock errors the game checks.
constexpr uint32_t X_WSAEINVAL = 10022;
constexpr uint32_t X_WSAEWOULDBLOCK = 10035;
constexpr uint32_t X_WSAENOTSOCK = 10038;
constexpr uint32_t X_WSAEMSGSIZE = 10040;
constexpr uint32_t X_WSAEADDRINUSE = 10048;
constexpr uint32_t X_WSAENETDOWN = 10050;
constexpr uint32_t X_WSAENOTCONN = 10057;
constexpr uint32_t X_WSANOTINITIALISED = 10093;

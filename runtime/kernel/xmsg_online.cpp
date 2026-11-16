// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// XGI (0xFB) and XLiveBase (0xFC) messages for online races through the
// SpeedBreaker online server (net/online.h). Message numbers and buffer
// layouts follow Xenia's netplay fork (apps/xgi_app.cc, xlivebase_app.cc;
// BSD-3-Clause, see THIRD_PARTY_NOTICES.md). Unknown messages fail with
// X_E_FAIL and a log line.
#include <stdafx.h>
#include "xmsg_online.h"
#include "function.h"
#include <net/online.h>
#include <net/sockets.h>
#include "imports_net.h"

#include <atomic>

namespace
{
    constexpr uint32_t X_E_SUCCESS = 0;
    constexpr uint32_t X_E_FAIL = 0x80004005;
    constexpr uint32_t X_E_INVALIDARG = 0x80070057;

    template<typename T>
    T* Guest(uint32_t address) { return static_cast<T*>(g_memory.Translate(address)); }
}

// The game finds races through EA's lobby (the online server's), not Live
// matchmaking: XSession messages only have to succeed, except that the
// host's XSessionCreate must hand back a real session (key id, the host's
// address, a key) and nonce, which the game passes to the others through
// the lobby (the race's SESS and PLATPARAMS).
uint32_t XgiDispatch(uint32_t message, uint32_t buffer, uint32_t length)
{
    switch (message)
    {
    case 0x000B0010: // XSessionCreate {session, flags, public, private, user, XSESSION_INFO*, nonce*}
    {
        if (!buffer)
            return X_E_INVALIDARG;
        auto* args = Guest<be<uint32_t>>(buffer);
        uint32_t flags = args[1], info = args[5], nonce = args[6];
        online::Identity id;
        if ((flags & 1) && info && online::GetIdentity(id))  // XSESSION_CREATE_HOST
        {
            static std::atomic<uint32_t> s_count{ 0 };
            uint32_t n = ++s_count;
            uint8_t* p = Guest<uint8_t>(info);
            // XNKID: an online peer session (0x90), this player's address, a count.
            uint8_t kid[8] = { 0x90, uint8_t(id.address >> 24), uint8_t(id.address >> 16), uint8_t(id.address >> 8),
                uint8_t(id.address), 0, uint8_t(n >> 8), uint8_t(n) };
            memcpy(p, kid, 8);
            online::FillXnAddr(id.address, *reinterpret_cast<XNADDR*>(p + 8));
            online::Random(p + 44, 16);  // XNKEY
            if (nonce)
                online::Random(Guest<uint8_t>(nonce), 8);
            fprintf(stderr, "[xgi] XSessionCreate as host (flags %08X, %u+%u slots)\n", flags, uint32_t(args[2]),
                uint32_t(args[3]));
        }
        return X_E_SUCCESS;
    }
    case 0x000B0006: // XUserSetContext
    case 0x000B0007: // XUserSetProperty
    case 0x000B0008: // achievements
    case 0x000B0011: // XSessionDelete
    case 0x000B0012: // XSessionJoin (local/remote)
    case 0x000B0013: // XSessionLeave (local/remote)
    case 0x000B0014: // XSessionStart
    case 0x000B0015: // XSessionEnd
    case 0x000B0016: // XSessionSearch
    case 0x000B0018: // XSessionModify
    case 0x000B0019: // invites
    case 0x000B001A: // XSessionArbitrationRegister
    case 0x000B001B: // XSessionSearchByID
    case 0x000B001C: // XSessionSearchEx
    case 0x000B0020: // stats
    case 0x000B0021: // XSessionWriteStats / leaderboards
    case 0x000B0025:
    case 0x000B0026:
        return X_E_SUCCESS;
    }
    fprintf(stderr, "[xgi] message %08X: success (buffer %08X, %u)\n", message, buffer, length);
    return X_E_SUCCESS;
}

uint32_t XLiveBaseDispatch(uint32_t message, uint32_t buffer, uint32_t length)
{
    switch (message)
    {
    case 0x00058003: // XLiveBaseLogonGetHR: S_OK once signed in
        return online::State() == online::Connection::Online ? X_E_SUCCESS : 0x80151000;  // XONLINE_E_LOGON_NOT_LOGGED_ON
    case 0x00058004: // XOnlineGetLogonID(u32*)
        if (buffer)
            *Guest<be<uint32_t>>(buffer) = 1;
        return X_E_SUCCESS;
    case 0x00058007: // XOnlineGetServiceInfo(service id, X_ONLINE_SERVICE_INFO*)
    {
        // Every service (EA's, for this game) is at the service address;
        // streams to it go to the online server (net/sockets.h).
        fprintf(stderr, "[xlive] XOnlineGetServiceInfo %08X\n", buffer);
        if (online::WaitConnected(10000) != online::Connection::Online)
            return 0x80151000;
        if (length)
        {
            auto* info = Guest<be<uint32_t>>(length);
            info[0] = buffer;
            info[1] = net::kServiceAddress;
            info[2] = uint32_t(buffer & 0xFFFF) << 16;  // port: the service's number; reserved 0
        }
        return X_E_SUCCESS;
    }
    case 0x00058037: // XPresenceInitializeLegacy
        return X_E_SUCCESS;
    case 0x0005800E: // XUserMuteListQuery(user, xuid, BOOL* muted): nobody is muted
    {
        // The game asks for every player in a race and reads the answer even
        // when this fails, so it must always be written.
        // buffer: {u32 user, pad, u64 xuid, u32 result}; length: the BOOL*.
        if (length)
            *Guest<be<uint32_t>>(length) = 0;
        if (buffer)
            Guest<be<uint32_t>>(buffer)[4] = 0;
        return X_E_SUCCESS;
    }
    case 0x00058020: // XFriendsCreateEnumerator: no friends list
    case 0x00058023: // game invite accepted info: none
        return X_E_FAIL;
    case 0x00058006: // XOnlineGetNatType(u32*): open (the relay needs no port)
        if (buffer)
            *Guest<be<uint32_t>>(buffer) = 1;  // XONLINE_NAT_OPEN
        return X_E_SUCCESS;
    }
    // Everything else (presence, the message sent as a game session starts,
    // 0x0005000D, and this game's 0x00050079 after the service lookup) is
    // answered with success and nothing written, as the NFSMW Recompiled
    // project found the game needs.
    fprintf(stderr, "[xlive] message %08X: success (buffer %08X, %u)\n", message, buffer, length);
    return X_E_SUCCESS;
}

// ---- Session handles ------------------------------------------------------------------
// The game makes a handle, takes the object behind it (passed back in the
// XSession messages, which don't look at it) and closes the handle.

#include "xdm.h"
#include "dispatcher.h"

uint32_t XamSessionCreateHandle(be<uint32_t>* handle)
{
    *handle = GetKernelHandle(CreateKernelObject<EventObject>(true, false));
    return 0;
}

uint32_t XamSessionRefObjByHandle(uint32_t handle, be<uint32_t>* object)
{
    *object = handle;
    return 0;
}

GUEST_FUNCTION_HOOK(__imp__XamSessionCreateHandle, XamSessionCreateHandle);
GUEST_FUNCTION_HOOK(__imp__XamSessionRefObjByHandle, XamSessionRefObjByHandle);

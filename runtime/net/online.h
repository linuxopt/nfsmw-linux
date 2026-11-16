// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// The connection to the SpeedBreaker online server (scripts/online/
// nfsmw_server.py), which stands in for Xbox Live: it gives each player an
// address, keeps the list of races (sessions), and relays the game's
// packets between players over UDP, so nobody needs an open port.
//
// online.toml in the data folder (next to settings.toml; the launcher's
// Online page writes it):
//     enabled = true                # false: online play off
//     server = "62.3.24.62:39300"   # host:port of the online server
//     name = "Racer1234"            # the name other players see (1-15 chars)
//     id = "E000..."                # this player's id (XUID), made on first use
// Online play is on when the file exists and isn't `enabled = false` (with
// kDefaultServer when it names none); NFSMW_ONLINE_SERVER overrides the server
// (and turns it on), NFSMW_ONLINE_NAME the name.
//
// Wire protocol (both sides; scripts/online/README.md has it in full):
//   TCP: frames of [u32 length][u8 op][u32 request id][body], big-endian.
//   UDP: [u8 kind][u32 token] then for data [u16 from port][u32 to address]
//        [u16 to port][payload]; the server forwards it as
//        [u8 kind][u32 from address][u16 from port][u16 to port][payload].
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

struct XNADDR;

namespace online
{
    // The server the game uses when online.toml names none (the launcher's
    // Online page offers the same one: launcher/core/online.h).
    constexpr const char* kDefaultServer = "62.3.24.62:39300";

    enum class Connection { Off, Connecting, Online, Failed };

    struct Identity
    {
        uint32_t address = 0;   // this player's address (10.x.y.z), host order
        uint64_t xuid = 0;
        std::string name;
    };

    // Reads online.toml and connects in the background (once; later calls
    // do nothing). Called when the game starts its network library.
    void Start();
    // online.toml names a server: the player shows as signed in to "Xbox
    // Live" with online.toml's name and id, and online play is allowed.
    bool Enabled();
    Connection State();
    // Waits (up to `timeoutMs`) while the connection is still being made;
    // returns the state then.
    Connection WaitConnected(int timeoutMs);
    // The server's reason when State() is Failed, for the log and toasts.
    std::string FailureReason();
    bool GetIdentity(Identity& out);
    // The name and id the game shows for the signed-in player: online.toml's
    // (made on first use), whether or not the server is reachable.
    std::string PlayerName();
    uint64_t PlayerXuid();

    // An XNADDR for a player's address (the address and the bytes the game
    // compares are all derived from it, so any player can rebuild another's).
    // The server's host and port from online.toml (false when off).
    bool ServerEndpoint(std::string& host, uint16_t& port);

    void FillXnAddr(uint32_t address, XNADDR& out);
    uint32_t AddressFromXnAddr(const XNADDR& addr);
    void Random(uint8_t* buffer, size_t length);

    // ---- Requests to the server -----------------------------------------------
    // Blocking request/reply on the control connection; false (and an empty
    // reply) when not online, on a timeout, or when the server refuses.
    bool Call(uint8_t op, const std::vector<uint8_t>& body, std::vector<uint8_t>& reply, int timeoutMs = 5000);

    // ---- Relay ------------------------------------------------------------------
    // Packets other players sent to this one: (from address, from port, to
    // port, bytes). Called on the network thread.
    using Receiver = std::function<void(uint32_t, uint16_t, uint16_t, const uint8_t*, size_t)>;
    void SetReceiver(Receiver receiver);
    // Sends `data` from this player's `fromPort` to `toAddress`:`toPort`
    // through the relay. False when not online or too big (over kMaxDatagram).
    bool SendDatagram(uint16_t fromPort, uint32_t toAddress, uint16_t toPort, const uint8_t* data, size_t length);
    constexpr size_t kMaxDatagram = 1264;

    // Message ops (TCP), shared with the server.
    namespace op
    {
        constexpr uint8_t Hello = 1;            // -> address, token, motd
        constexpr uint8_t Ping = 2;
        constexpr uint8_t SessionCreate = 10;
        constexpr uint8_t SessionModify = 11;
        constexpr uint8_t SessionDelete = 12;
        constexpr uint8_t SessionSearch = 13;
        constexpr uint8_t SessionDetails = 14;
        constexpr uint8_t SessionJoin = 15;
        constexpr uint8_t SessionLeave = 16;
        constexpr uint8_t SessionStart = 17;
        constexpr uint8_t SessionEnd = 18;
        constexpr uint8_t QosListen = 20;
        constexpr uint8_t QosLookup = 21;
        constexpr uint8_t Error = 0xFF;          // reply: [u16 length][utf-8 reason]
    }

    // Big-endian writer/reader for message bodies.
    struct Writer
    {
        std::vector<uint8_t> bytes;
        void U8(uint8_t v) { bytes.push_back(v); }
        void U16(uint16_t v) { U8(v >> 8); U8(uint8_t(v)); }
        void U32(uint32_t v) { U16(v >> 16); U16(uint16_t(v)); }
        void U64(uint64_t v) { U32(uint32_t(v >> 32)); U32(uint32_t(v)); }
        void Bytes(const void* p, size_t n) { U32(uint32_t(n)); Raw(p, n); }
        void Raw(const void* p, size_t n) { auto* b = static_cast<const uint8_t*>(p); bytes.insert(bytes.end(), b, b + n); }
        void Str(const std::string& s) { Bytes(s.data(), s.size()); }
    };

    struct Reader
    {
        const uint8_t* p;
        size_t n;
        bool ok = true;
        Reader(const std::vector<uint8_t>& v) : p(v.data()), n(v.size()) {}
        bool Need(size_t k) { if (!ok || n < k) { ok = false; return false; } return true; }
        uint8_t U8() { if (!Need(1)) return 0; uint8_t v = *p; p++; n--; return v; }
        uint16_t U16() { uint16_t h = U8(); return uint16_t(h << 8 | U8()); }
        uint32_t U32() { uint32_t h = U16(); return h << 16 | U16(); }
        uint64_t U64() { uint64_t h = U32(); return h << 32 | U32(); }
        std::vector<uint8_t> Bytes(size_t cap = 1 << 20)
        {
            uint32_t k = U32();
            if (k > cap || !Need(k)) { ok = false; return {}; }
            std::vector<uint8_t> v(p, p + k); p += k; n -= k; return v;
        }
        std::string Str() { auto v = Bytes(4096); return std::string(v.begin(), v.end()); }
        bool Raw(void* out, size_t k) { if (!Need(k)) return false; memcpy(out, p, k); p += k; n -= k; return true; }
    };
}

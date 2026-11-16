#!/usr/bin/env python3
"""SpeedBreaker online server: stands in for Xbox Live and EA's servers for
Need for Speed: Most Wanted (SpeedBreaker builds).

    python3 nfsmw_server.py [--host 0.0.0.0] [--port 39300] [--motd TEXT]

Players' games sign in over TCP on --port and send their race traffic over
UDP to the same port; the server relays it to the other players, so nobody
needs to open a port at home. The game's online menus talk to EA's lobby on
--port + 1 (nfsmw_lobby.py). Standard library
only (Python 3.9+). README.md next to this file describes
hosting it and the protocol.

Environment: NFSMW_SERVER_PORT, NFSMW_SERVER_MOTD (the same as the options).
"""
import argparse
import asyncio
import ipaddress
import logging
import os
import secrets
import struct
import sys
import time

# The lobby lives next to this file (found even under python3 -I).
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import nfsmw_lobby  # noqa: E402

PROTOCOL = 1

# TCP ops (runtime/net/online.h).
OP_HELLO = 1
OP_PING = 2
OP_SESSION_CREATE = 10
OP_SESSION_MODIFY = 11
OP_SESSION_DELETE = 12
OP_SESSION_SEARCH = 13
OP_SESSION_DETAILS = 14
OP_SESSION_JOIN = 15
OP_SESSION_LEAVE = 16
OP_SESSION_START = 17
OP_SESSION_END = 18
OP_QOS_LISTEN = 20
OP_QOS_LOOKUP = 21
OP_ERROR = 0xFF

# UDP kinds.
UDP_KEEPALIVE = 1
UDP_DATA = 2

MAX_FRAME = 256 * 1024
MAX_DATAGRAM = 1264
MAX_PLAYERS = 256
MAX_SESSIONS = 128
MAX_QOS_DATA = 1024
UDP_TIMEOUT = 30.0          # seconds without a keepalive: relay address forgotten
PACKETS_PER_SECOND = 2000   # per player, relay flood guard
ADDRESS_NET = ipaddress.ip_network("10.80.0.0/16")

log = logging.getLogger("nfsmw")


class ProtocolError(Exception):
    pass


class Reader:
    def __init__(self, data):
        self.data = data
        self.at = 0

    def take(self, n):
        if self.at + n > len(self.data):
            raise ProtocolError("message too short")
        b = self.data[self.at:self.at + n]
        self.at += n
        return b

    def u8(self):
        return self.take(1)[0]

    def u16(self):
        return struct.unpack(">H", self.take(2))[0]

    def u32(self):
        return struct.unpack(">I", self.take(4))[0]

    def u64(self):
        return struct.unpack(">Q", self.take(8))[0]

    def bytes(self, cap=65536):
        n = self.u32()
        if n > cap:
            raise ProtocolError("field too long")
        return self.take(n)

    def str(self, cap=256):
        return self.bytes(cap).decode("utf-8", "replace")


class Writer:
    def __init__(self):
        self.parts = []

    def u8(self, v):
        self.parts.append(struct.pack(">B", v))
        return self

    def u16(self, v):
        self.parts.append(struct.pack(">H", v))
        return self

    def u32(self, v):
        self.parts.append(struct.pack(">I", v))
        return self

    def u64(self, v):
        self.parts.append(struct.pack(">Q", v))
        return self

    def bytes(self, b):
        self.u32(len(b))
        self.parts.append(bytes(b))
        return self

    def str(self, s):
        return self.bytes(s.encode("utf-8"))

    def raw(self, b):
        self.parts.append(bytes(b))
        return self

    def done(self):
        return b"".join(self.parts)


def clean_name(name):
    out = "".join(c for c in name if 0x20 <= ord(c) < 0x7F)[:15].strip()
    return out


def fmt_addr(a):
    return str(ipaddress.ip_address(a))


class Player:
    def __init__(self, server, writer, name, xuid, address):
        self.server = server
        self.writer = writer
        peer = writer.get_extra_info("peername")
        self.host = peer[0] if peer else None   # the lobby connection must come from here too
        self.name = name
        self.xuid = xuid
        self.address = address              # 10.80.x.y as an int
        self.token = secrets.randbits(32)
        self.udp = None                     # (host, port) seen in keepalives
        self.udp_seen = 0.0
        self.window_start = 0.0
        self.window_count = 0
        self.sessions = set()               # session ids this player hosts
        self.qos_data = b""                 # XNetQosListen data (lobby info)

    def __repr__(self):
        return f"{self.name} ({fmt_addr(self.address)})"


class Session:
    def __init__(self, sid, host, flags, public_slots, private_slots, nonce, key, attrs):
        self.id = sid                # 8-byte XNKID as an int
        self.host = host             # Player
        self.flags = flags
        self.public_slots = public_slots
        self.private_slots = private_slots
        self.nonce = nonce
        self.key = key               # 16-byte XNKEY (the host's)
        self.attrs = attrs           # contexts and properties blob (opaque)
        self.members = {}            # xuid -> private flag
        self.started = False
        self.created = time.time()

    def open_public(self):
        used = sum(1 for p in self.members.values() if not p)
        return max(0, self.public_slots - used)

    def open_private(self):
        used = sum(1 for p in self.members.values() if p)
        return max(0, self.private_slots - used)


class Server:
    def __init__(self, motd=""):
        self.motd = motd
        self.players = {}            # address -> Player
        self.sessions = {}           # id -> Session
        self.transport = None        # UDP
        self.lobby = nfsmw_lobby.Lobby(self)

    # ---- addresses -------------------------------------------------------------

    def new_address(self):
        hosts = ADDRESS_NET.num_addresses
        base = int(ADDRESS_NET.network_address)
        for _ in range(1000):
            a = base + 2 + secrets.randbelow(hosts - 4)
            if a not in self.players:
                return a
        raise ProtocolError("the server is full")

    # ---- TCP -------------------------------------------------------------------

    async def handle(self, reader, writer):
        peer = writer.get_extra_info("peername")
        player = None
        try:
            op, rid, body = await self.read_frame(reader, timeout=15)
            if op != OP_HELLO:
                raise ProtocolError("expected hello")
            r = Reader(body)
            version = r.u32()
            name = clean_name(r.str(64))
            xuid = r.u64()
            if version != PROTOCOL:
                await self.send(writer, OP_ERROR, rid, Writer().str(
                    "this game is a different version from the server's; update SpeedBreaker").done())
                return
            if not name:
                await self.send(writer, OP_ERROR, rid, Writer().str("pick a name in online.toml").done())
                return
            if len(self.players) >= MAX_PLAYERS:
                await self.send(writer, OP_ERROR, rid, Writer().str("the server is full").done())
                return
            for other in list(self.players.values()):
                if other.xuid != xuid and other.name.lower() == name.lower():
                    await self.send(writer, OP_ERROR, rid, Writer().str(
                        f"someone online is already called {name}; pick another name in online.toml").done())
                    return
            for other in list(self.players.values()):
                if other.xuid == xuid:
                    log.info("%s signed in again from %s; dropping the old connection", other, peer)
                    self.drop(other)
                    other.writer.close()
            player = Player(self, writer, name, xuid, self.new_address())
            self.players[player.address] = player
            log.info("%s signed in from %s", player, peer)
            await self.send(writer, OP_HELLO, rid, Writer().u32(player.address).u32(player.token).str(self.motd).done())
            while True:
                op, rid, body = await self.read_frame(reader, timeout=None)
                try:
                    reply_op, reply = self.request(player, op, Reader(body))
                except ProtocolError as e:
                    reply_op, reply = OP_ERROR, Writer().str(str(e)).done()
                await self.send(writer, reply_op, rid, reply)
        except (asyncio.IncompleteReadError, ConnectionError, asyncio.TimeoutError):
            pass
        except ProtocolError as e:
            log.info("%s: %s", player or peer, e)
        finally:
            if player is not None:
                log.info("%s left", player)
                self.drop(player)
            writer.close()

    async def read_frame(self, reader, timeout):
        head = await asyncio.wait_for(reader.readexactly(4), timeout)
        (length,) = struct.unpack(">I", head)
        if length < 5 or length > MAX_FRAME:
            raise ProtocolError("bad frame length")
        frame = await asyncio.wait_for(reader.readexactly(length), timeout or 60)
        return frame[0], struct.unpack(">I", frame[1:5])[0], frame[5:]

    async def send(self, writer, op, rid, body):
        writer.write(struct.pack(">IBI", 5 + len(body), op, rid) + body)
        await writer.drain()

    def drop(self, player):
        if self.players.get(player.address) is player:
            del self.players[player.address]
        for sid in list(player.sessions):
            self.sessions.pop(sid, None)
        for s in self.sessions.values():
            s.members.pop(player.xuid, None)

    # ---- requests --------------------------------------------------------------

    def request(self, player, op, r):
        if op == OP_PING:
            return OP_PING, b""
        handler = {
            OP_SESSION_CREATE: self.session_create,
            OP_SESSION_MODIFY: self.session_modify,
            OP_SESSION_DELETE: self.session_delete,
            OP_SESSION_SEARCH: self.session_search,
            OP_SESSION_DETAILS: self.session_details,
            OP_SESSION_JOIN: self.session_join,
            OP_SESSION_LEAVE: self.session_leave,
            OP_SESSION_START: self.session_start,
            OP_SESSION_END: self.session_end,
            OP_QOS_LISTEN: self.qos_listen,
            OP_QOS_LOOKUP: self.qos_lookup,
        }.get(op)
        if handler is None:
            raise ProtocolError(f"unknown request {op}")
        return op, handler(player, r)

    def own_session(self, player, sid):
        s = self.sessions.get(sid)
        if s is None:
            raise ProtocolError("no such race")
        if s.host is not player:
            raise ProtocolError("not your race")
        return s

    def write_session(self, w, s):
        w.u64(s.id).u32(s.host.address).str(s.host.name).u64(s.host.xuid)
        w.u32(s.flags).u32(s.public_slots).u32(s.private_slots)
        w.u32(s.open_public()).u32(s.open_private())
        w.u64(s.nonce).raw(s.key).u8(1 if s.started else 0)
        w.bytes(s.attrs)
        w.u32(len(s.members))
        for xuid, private in s.members.items():
            w.u64(xuid).u8(1 if private else 0)

    def session_create(self, player, r):
        # [u32 flags][u32 public][u32 private][u64 nonce][16 key][bytes attrs]
        flags, public, private = r.u32(), r.u32(), r.u32()
        nonce = r.u64()
        key = r.take(16)
        attrs = r.bytes()
        if len(self.sessions) >= MAX_SESSIONS:
            raise ProtocolError("the server has too many races")
        if public > 32 or private > 32:
            raise ProtocolError("too many slots")
        sid = secrets.randbits(64)
        s = Session(sid, player, flags, public, private, nonce, key, attrs)
        self.sessions[sid] = s
        player.sessions.add(sid)
        log.info("%s opened race %016X (%d slots)", player, sid, public + private)
        w = Writer()
        self.write_session(w, s)
        return w.done()

    def session_modify(self, player, r):
        sid = r.u64()
        s = self.own_session(player, sid)
        s.flags = r.u32()
        s.public_slots = min(32, r.u32())
        s.private_slots = min(32, r.u32())
        attrs = r.bytes()
        if attrs:
            s.attrs = attrs
        return b""

    def session_delete(self, player, r):
        sid = r.u64()
        s = self.sessions.get(sid)
        if s is not None and s.host is player:
            del self.sessions[sid]
            player.sessions.discard(sid)
            log.info("%s closed race %016X", player, sid)
        return b""

    def session_search(self, player, r):
        # [u32 max results]; the game filters by its own query (attrs).
        limit = min(r.u32(), 50)
        found = [s for s in self.sessions.values()
                 if s.host is not player and (s.open_public() > 0 or s.public_slots == 0)]
        found.sort(key=lambda s: -s.created)
        w = Writer().u32(len(found[:limit]))
        for s in found[:limit]:
            self.write_session(w, s)
        return w.done()

    def session_details(self, player, r):
        sid = r.u64()
        s = self.sessions.get(sid)
        if s is None:
            raise ProtocolError("no such race")
        w = Writer()
        self.write_session(w, s)
        return w.done()

    def session_join(self, player, r):
        # [u64 id][u32 n]([u64 xuid][u8 private])*
        sid = r.u64()
        s = self.sessions.get(sid)
        if s is None:
            raise ProtocolError("no such race")
        for _ in range(min(r.u32(), 16)):
            xuid, private = r.u64(), r.u8()
            s.members[xuid] = bool(private)
        return b""

    def session_leave(self, player, r):
        sid = r.u64()
        s = self.sessions.get(sid)
        if s is not None:
            for _ in range(min(r.u32(), 16)):
                s.members.pop(r.u64(), None)
        return b""

    def session_start(self, player, r):
        s = self.sessions.get(r.u64())
        if s is not None and s.host is player:
            s.started = True
        return b""

    def session_end(self, player, r):
        s = self.sessions.get(r.u64())
        if s is not None and s.host is player:
            s.started = False
        return b""

    def qos_listen(self, player, r):
        data = r.bytes(MAX_QOS_DATA)
        player.qos_data = bytes(data)
        return b""

    def qos_lookup(self, player, r):
        # [u32 n]([u32 address])* -> [u32 n]([u8 found][bytes data])*
        n = min(r.u32(), 64)
        w = Writer().u32(n)
        for _ in range(n):
            other = self.players.get(r.u32())
            if other is None:
                w.u8(0).bytes(b"")
            else:
                w.u8(1).bytes(other.qos_data)
        return w.done()

    # ---- UDP relay ---------------------------------------------------------------

    def datagram(self, data, addr):
        if len(data) < 5:
            return
        kind = data[0]
        (token,) = struct.unpack(">I", data[1:5])
        if kind == UDP_KEEPALIVE:
            for p in self.players.values():
                if p.token == token:
                    p.udp = addr
                    p.udp_seen = time.monotonic()
                    return
            return
        if kind != UDP_DATA or len(data) < 13 or len(data) > 13 + MAX_DATAGRAM:
            return
        sender = None
        for p in self.players.values():
            if p.token == token:
                sender = p
                break
        if sender is None:
            return
        now = time.monotonic()
        sender.udp = addr          # the relay address follows the sender's NAT
        sender.udp_seen = now
        if now - sender.window_start >= 1.0:
            sender.window_start = now
            sender.window_count = 0
        sender.window_count += 1
        if sender.window_count > PACKETS_PER_SECOND:
            return
        from_port, to_addr, to_port = struct.unpack(">HIH", data[5:13])
        target = self.players.get(to_addr)
        if target is None or target.udp is None or now - target.udp_seen > UDP_TIMEOUT:
            return
        self.transport.sendto(struct.pack(">BIHH", UDP_DATA, sender.address, from_port, to_port) + data[13:], target.udp)


class RelayProtocol(asyncio.DatagramProtocol):
    def __init__(self, server):
        self.server = server

    def connection_made(self, transport):
        self.server.transport = transport

    def datagram_received(self, data, addr):
        try:
            self.server.datagram(data, addr)
        except Exception:  # never let one packet stop the relay
            log.exception("relay")


async def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--host", default="0.0.0.0")
    ap.add_argument("--port", type=int, default=int(os.environ.get("NFSMW_SERVER_PORT", "39300")))
    ap.add_argument("--motd", default=os.environ.get("NFSMW_SERVER_MOTD", ""))
    ap.add_argument("--trace", action="store_true", help="log every lobby message")
    args = ap.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(message)s")
    if args.trace:
        logging.getLogger("nfsmw.lobby").setLevel(logging.DEBUG)
    server = Server(args.motd)
    loop = asyncio.get_running_loop()
    await loop.create_datagram_endpoint(lambda: RelayProtocol(server), local_addr=(args.host, args.port))
    tcp = await asyncio.start_server(server.handle, args.host, args.port)
    lobby = await asyncio.start_server(server.lobby.handle, args.host, args.port + 1)
    log.info("SpeedBreaker online server on %s: %d (sign-in TCP, relay UDP), %d (EA lobby TCP)",
             args.host, args.port, args.port + 1)
    async with tcp, lobby:
        await asyncio.gather(tcp.serve_forever(), lobby.serve_forever())


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass

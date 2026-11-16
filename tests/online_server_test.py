"""Tests for the online server (scripts/online/nfsmw_server.py): sign-in,
names, the relay between two players, and EA's lobby (nfsmw_lobby.py) from
login to the start line. Runs the server in-process on free ports.

    python3 -B tests/online_server_test.py
"""
import asyncio
import base64
import pathlib
import socket
import struct
import sys
import unittest
import urllib.parse

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts" / "online"))
import nfsmw_server as srv  # noqa: E402
import nfsmw_lobby as lobby  # noqa: E402


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class Client:
    """A game's connection to the server, as runtime/net/online.cpp makes it."""

    def __init__(self, port):
        self.port = port

    async def hello(self, name, xuid, version=srv.PROTOCOL):
        self.reader, self.writer = await asyncio.open_connection("127.0.0.1", self.port)
        body = srv.Writer().u32(version).str(name).u64(xuid).done()
        await self.send(srv.OP_HELLO, 0, body)
        op, rid, body = await self.read()
        if op == srv.OP_ERROR:
            return srv.Reader(body).str()
        r = srv.Reader(body)
        self.address, self.token, self.motd = r.u32(), r.u32(), r.str()
        return None

    async def send(self, op, rid, body):
        self.writer.write(struct.pack(">IBI", 5 + len(body), op, rid) + body)
        await self.writer.drain()

    async def read(self):
        (length,) = struct.unpack(">I", await self.reader.readexactly(4))
        frame = await self.reader.readexactly(length)
        return frame[0], struct.unpack(">I", frame[1:5])[0], frame[5:]

    async def call(self, op, body):
        await self.send(op, 7, body)
        rop, rid, rbody = await self.read()
        assert rid == 7
        return rop, rbody

    def close(self):
        self.writer.close()


class ServerCase(unittest.IsolatedAsyncioTestCase):
    """A server on free ports."""

    async def asyncSetUp(self):
        self.port = free_port()
        self.server = srv.Server("hello there")
        loop = asyncio.get_running_loop()
        self.udp, _ = await loop.create_datagram_endpoint(
            lambda: srv.RelayProtocol(self.server), local_addr=("127.0.0.1", self.port))
        self.tcp = await asyncio.start_server(self.server.handle, "127.0.0.1", self.port)

    async def asyncTearDown(self):
        self.tcp.close()
        await self.tcp.wait_closed()
        self.udp.close()


class ServerTest(ServerCase):
    async def test_sign_in_gives_distinct_addresses(self):
        a, b = Client(self.port), Client(self.port)
        self.assertIsNone(await a.hello("Alice", 0xE000000000000001))
        self.assertIsNone(await b.hello("Bob", 0xE000000000000002))
        self.assertEqual(a.motd, "hello there")
        self.assertNotEqual(a.address, b.address)
        for c in (a, b):
            self.assertEqual(c.address >> 16, 0x0A50, "in 10.80.0.0/16")
        a.close()
        b.close()

    async def test_a_name_in_use_is_refused(self):
        a, b = Client(self.port), Client(self.port)
        self.assertIsNone(await a.hello("Alice", 0xE000000000000001))
        error = await b.hello("alice", 0xE000000000000002)
        self.assertIn("already called", error)
        a.close()

    async def test_the_same_player_again_replaces_the_old_connection(self):
        a, b = Client(self.port), Client(self.port)
        self.assertIsNone(await a.hello("Alice", 0xE000000000000001))
        self.assertIsNone(await b.hello("Alice", 0xE000000000000001))
        await asyncio.sleep(0.05)
        self.assertEqual(len(self.server.players), 1)
        b.close()

    async def test_wrong_version_is_refused(self):
        a = Client(self.port)
        self.assertIn("different version", await a.hello("Alice", 1, version=99))

    async def test_relay_carries_a_datagram_between_players(self):
        a, b = Client(self.port), Client(self.port)
        await a.hello("Alice", 0xE000000000000001)
        await b.hello("Bob", 0xE000000000000002)
        ua = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        ub = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        for u in (ua, ub):
            u.setblocking(False)
            u.connect(("127.0.0.1", self.port))
        ua.send(struct.pack(">BI", srv.UDP_KEEPALIVE, a.token))
        ub.send(struct.pack(">BI", srv.UDP_KEEPALIVE, b.token))
        await asyncio.sleep(0.05)
        # Alice's port 3658 to Bob's port 3658.
        ua.send(struct.pack(">BIHIH", srv.UDP_DATA, a.token, 3658, b.address, 3658) + b"race!")
        await asyncio.sleep(0.05)
        data = ub.recv(2048)
        kind, sender, from_port, to_port = struct.unpack(">BIHH", data[:9])
        self.assertEqual((kind, sender, from_port, to_port, data[9:]), (srv.UDP_DATA, a.address, 3658, 3658, b"race!"))
        # A forged token goes nowhere.
        ua.send(struct.pack(">BIHIH", srv.UDP_DATA, a.token ^ 1, 3658, b.address, 3658) + b"x")
        await asyncio.sleep(0.05)
        with self.assertRaises(BlockingIOError):
            ub.recv(2048)
        ua.close()
        ub.close()
        a.close()
        b.close()

    async def test_qos_data_is_shared(self):
        a, b = Client(self.port), Client(self.port)
        await a.hello("Alice", 0xE000000000000001)
        await b.hello("Bob", 0xE000000000000002)
        op, _ = await a.call(srv.OP_QOS_LISTEN, srv.Writer().bytes(b"lobby").done())
        self.assertEqual(op, srv.OP_QOS_LISTEN)
        op, body = await b.call(srv.OP_QOS_LOOKUP, srv.Writer().u32(2).u32(a.address).u32(0x0A500999).done())
        r = srv.Reader(body)
        self.assertEqual(r.u32(), 2)
        self.assertEqual((r.u8(), r.bytes()), (1, b"lobby"))
        self.assertEqual((r.u8(), r.bytes()), (0, b""))
        a.close()
        b.close()


class Lobby:
    """A game's connection to EA's lobby (scripts/online/nfsmw_lobby.py)."""

    def __init__(self, port):
        self.port = port
        self.buffer = b""

    async def open(self):
        self.reader, self.writer = await asyncio.open_connection("127.0.0.1", self.port)

    def send(self, command, **fields):
        self.writer.write(lobby.encode(command, fields))

    async def read(self, timeout=2.0):
        while True:
            if len(self.buffer) >= 12:
                (length,) = struct.unpack(">I", self.buffer[8:12])
                if len(self.buffer) >= length:
                    m, self.buffer = self.buffer[:length], self.buffer[length:]
                    return m[:4].decode(), m[4:8], lobby.decode_body(m[12:])
            self.buffer += await asyncio.wait_for(self.reader.read(65536), timeout)

    async def until(self, command):
        """Messages up to and including the first `command`."""
        seen = []
        while True:
            m = await self.read()
            seen.append(m)
            if m[0] == command:
                return seen

    async def quiet(self):
        """Every message that arrives within a moment."""
        seen = []
        try:
            while True:
                seen.append(await self.read(0.2))
        except asyncio.TimeoutError:
            return seen

    def close(self):
        self.writer.close()


def named(messages, command):
    return [f for c, _, f in messages if c == command]


class LobbyTest(ServerCase):
    async def asyncSetUp(self):
        await super().asyncSetUp()
        self.ea = await asyncio.start_server(self.server.lobby.handle, "127.0.0.1", 0)
        self.ea_port = self.ea.sockets[0].getsockname()[1]

    async def asyncTearDown(self):
        self.ea.close()
        await self.ea.wait_closed()
        await super().asyncTearDown()

    async def login(self, name, xuid):
        c = Client(self.port)
        self.assertIsNone(await c.hello(name, xuid))
        l = Lobby(self.ea_port)
        await l.open()
        l.send("@tic")
        l.send("@dir", PROD="nfs-xbox-2006")
        _, _, d = await l.read()
        self.assertEqual((d["ADDR"], d["PORT"]), ("10.80.0.1", "10901"))
        l.send("auth", GTAG=name, MADDR=f"${xuid:016x}^xnaddr-of-{name}")
        cmd, sub, a = await l.read()
        self.assertEqual((cmd, sub, a.get("NAME")), ("auth", b"\0\0\0\0", name))
        address = f"{c.address >> 24}.{(c.address >> 16) & 255}.{(c.address >> 8) & 255}.{c.address & 255}"
        self.assertEqual(a["ADDR"], address, "the relay address, not the real one")
        l.send("pers", PERS=name)
        await l.until("pers")
        l.send("sele", MYGAME=1, ROOMS=1)
        who = named(await l.until("+who"), "+who")[0]
        self.assertEqual((who["MA"], who["LA"]), (f"${xuid:016x}^xnaddr-of-{name}", address))
        return c, l, address

    async def test_a_lobby_login_needs_a_sign_in(self):
        l = Lobby(self.ea_port)
        await l.open()
        l.send("auth", GTAG="Nobody", MADDR="$0009000000000099^x")
        cmd, sub, _ = await l.read()
        self.assertEqual(cmd, "auth")
        self.assertNotEqual(sub, b"\0\0\0\0")
        l.close()

    async def test_host_and_joiner_reach_the_start_line(self):
        hc, h, h_addr = await self.login("Host", 0x0009000000000001)
        jc, j, j_addr = await self.login("Joiner", 0x0009000000000002)

        # The host's Quick Match finds nothing, then creates a room and a race.
        h.send("usea", START=0, COUNT=100)
        self.assertEqual((await h.read())[2]["COUNT"], "0")
        h.send("ucre", NAME="049.Host", SIZE=4, PARAMS="V%3d192641%0aM%3d2%0a")
        m = await h.until("+usm")
        room = named(m, "ucre")[0]
        self.assertEqual((room["O"], room["N"], room["C"]), ("Host", "049.Host", "1"))
        h.send("gcre", NAME="Host", MAXSIZE=4, MINSIZE=2)
        game = named(await h.until("+who"), "gcre")[0]
        self.assertEqual((game["HOST"], game["COUNT"], game["ADDR0"]), ("Host", "1", h_addr))
        h.send("gset", SESS="$host-session", PLATPARAMS="pp")
        self.assertEqual((await h.read())[2]["SESS"], "$host-session")

        # The joiner finds it, subscribes, joins the room and the race.
        j.send("usea", START=0, COUNT=100)
        m = await j.until("+uss")
        self.assertEqual(named(m, "+uss")[0]["N"], "049.Host")
        j.send("sele", USERSET0="049.Host")
        m = await j.until("+usm")
        self.assertEqual(named(m, "+usm")[0]["N"], "Host")
        j.send("ujoi", NAME="049.Host")
        m = await j.until("+usm")
        self.assertEqual(named(m, "ujoi")[0]["C"], "2")
        self.assertIn("Host", [u["N"] for u in named(m, "+usr")])
        pushed = await h.quiet()
        self.assertEqual(named(pushed, "+usr")[0]["LA"], j_addr, "the host learns the joiner's relay address")
        self.assertEqual(named(pushed, "+usm")[0]["N"], "Joiner")
        j.send("gjoi", NAME="Host")
        m = await j.until("+who")
        g = named(m, "gjoi")[0]
        self.assertEqual((g["SELF"], g["SESS"], g["COUNT"], g["OPPO1"], g["ADDR1"]),
                         ("Joiner", "$host-session", "2", "Joiner", j_addr))
        self.assertNotIn("+ses", [c for c, _, _ in m], "no +ses before the host starts")
        self.assertEqual(named(m, "+who")[0]["G"], g["IDENT"])
        self.assertEqual(named(await h.quiet(), "+mgm")[0]["COUNT"], "2")
        j.send("mesg", TEXT="42", ATTR="EGS")
        await j.until("mesg")
        self.assertEqual(named(await h.quiet(), "+msg")[0], {"N": "Joiner", "T": "42", "F": "EGS"})

        # The host starts: +ses to both.
        h.send("gsta")
        m = await h.until("gsta")
        self.assertEqual(named(m, "+ses")[0]["SELF"], "Host")
        self.assertEqual(named(await j.quiet(), "+ses")[0]["SELF"], "Joiner")

        # Results: each reports its own place; the leaderboard shows both.
        def resu(me, places):
            blob = bytes([3, len(places), 1, me, 7, 0xD0, 0, 1])
            for index, place in enumerate(places):
                blob += bytes([index, place]) + bytes(48) + bytes([0])
            return urllib.parse.quote(base64.b64encode(blob).decode())
        h.send("rank", RESU=resu(0, [2, 1]))
        j.send("rank", RESU=resu(1, [2, 1]))
        await h.until("rank")
        await j.until("rank")
        h.send("snap", CHAN=7, START=0, RANGE=10)
        m = await h.until("snap")
        m += await h.quiet()
        self.assertEqual(named(m, "snap")[0]["CHAN"], "7")
        rows = named(m, "+snp")
        self.assertEqual([(r["N"], r["R"], r["P"]) for r in rows], [("Joiner", "1", "20"), ("Host", "2", "10")])

        # The host leaves: the race and the room go.
        h.close()
        await asyncio.sleep(0.1)
        self.assertEqual((self.server.lobby.games, self.server.lobby.rooms), ({}, {}))
        j.close()
        hc.close()
        jc.close()


if __name__ == "__main__":
    unittest.main()

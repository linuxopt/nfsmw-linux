"""EA's lobby for Need for Speed: Most Wanted (Xbox 360, PROD=nfs-xbox-2006),
the part of the SpeedBreaker online server the game's online menus talk to.

The game speaks EA's 2003-2006 lobby protocol ("Aries"/DirtySock): messages
of a 12-byte header (4-character command, 4-byte subcode, 4-byte big-endian
total length) and a body of KEY=value lines ending in a NUL. It first asks
the directory (@dir) where the lobby is, then logs in to the lobby, lists and
creates races ("rooms" holding one "game" each), and starts them; the race
itself goes player to player over UDP (port 3658), which the server's relay
carries. Every address given to the game is the player's relay address
(10.80.x.y), never their real one.

Written from the protocol as the NFSMW Recompiled project documented it
(github.com/victorgbd/nfsmw-server-lan); README.md here summarises it.
"""
import asyncio
import base64
import logging
import secrets
import struct
import time
import urllib.parse

log = logging.getLogger("nfsmw.lobby")

SERVICE_ADDRESS = "10.80.0.1"      # where the game is told the lobby is (runtime/net/sockets.h)
LOBBY_PORT = 10901
HEARTBEAT = 20.0                   # seconds between ~png; the game gives up after 60 s of silence
MAX_MESSAGE = 64 * 1024
STAT_COUNT = 44
EPOCH = "2005.11.15-00:00:00"


def now_text():
    return time.strftime("%Y.%m.%d-%H:%M:%S", time.gmtime())


def encode(command, fields=None, subcode=b"\0\0\0\0"):
    body = "".join(f"{k}={v}\n" for k, v in (fields or {}).items()).encode("latin-1", "replace") + b"\0"
    return command.encode("ascii") + subcode + struct.pack(">I", 12 + len(body)) + body


def decode_body(body):
    fields = {}
    for line in body.rstrip(b"\0").decode("latin-1").split("\n"):
        if "=" in line:
            k, v = line.split("=", 1)
            fields[k] = v
    return fields


def hex_stats(values):
    return ",".join(("-" + format(-v, "x")) if v < 0 else format(v, "x") for v in values)


def clean(value, limit=256):
    """A value safe to echo: no newline or NUL, bounded."""
    return value.replace("\n", "").replace("\0", "")[:limit]


class Stats:
    """Per-name race results, in memory (the lobby's leaderboards)."""

    def __init__(self):
        self.players = {}   # lower name -> {"name", "points", "wins", "losses", "last"}

    def get(self, name):
        return self.players.setdefault(name.lower(), {"name": name, "points": 0, "wins": 0, "losses": 0, "last": 0})

    def vector(self, name):
        s = self.get(name)
        v = [0] * STAT_COUNT
        v[8] = s["points"]      # first race-mode block: points, ranked wins, ranked losses
        v[9] = s["wins"]
        v[10] = s["losses"]
        return v

    def record(self, name, position, players):
        s = self.get(name)
        s["points"] += max(0, players - position + 1) * 10
        if position == 1:
            s["wins"] += 1
        else:
            s["losses"] += 1
        s["last"] = int(time.time())

    def board(self):
        return sorted(self.players.values(), key=lambda s: (-s["points"], -s["wins"], s["name"].lower()))


class Room:
    def __init__(self, ident, owner, fields):
        self.ident = ident
        self.owner = owner                      # Connection
        self.type = clean(fields.get("TYPE", "0"))
        self.sysflags = clean(fields.get("SYSFLAGS", "0"))
        self.custflags = clean(fields.get("CUSTFLAGS", "0"))
        self.size = clean(fields.get("SIZE", "4"))
        self.name = clean(fields.get("NAME", owner.name), 64)
        self.desc = ""
        self.params = clean(fields.get("PARAMS", ""), 2048)
        self.members = [owner]

    def record(self):
        return {"I": self.ident, "T": self.type, "SF": self.sysflags, "CF": self.custflags, "O": self.owner.name,
                "S": self.size, "C": max(1, len(self.members)), "N": self.name, "D": self.desc, "P": self.params}


class Game:
    KEYS = ("PARAMS", "PLATPARAMS", "CUSTFLAGS", "SYSFLAGS", "MINSIZE", "MAXSIZE", "SESS", "PRIV")

    def __init__(self, ident, host, room, fields):
        self.ident = ident
        self.name = clean(fields.get("NAME", host.name), 64)
        self.host = host
        self.room = room
        self.players = [host]
        self.seed = secrets.randbelow(2**31 - 2) + 1
        self.when = now_text()
        self.values = {"PARAMS": "", "PLATPARAMS": "", "CUSTFLAGS": "0", "SYSFLAGS": "0", "MINSIZE": "2",
                       "MAXSIZE": "4", "SESS": "", "PRIV": "0"}
        self.update(fields)

    def update(self, fields):
        for k, v in fields.items():
            if k in self.KEYS:
                self.values[k] = clean(v, 4096)

    def record(self, recipient):
        v = self.values
        r = {"IDENT": self.ident, "NAME": self.name, "SELF": recipient.name, "HOST": self.host.name,
             "PARAMS": v["PARAMS"], "PLATPARAMS": v["PLATPARAMS"], "ROOM": self.room.ident if self.room else 0,
             "CUSTFLAGS": v["CUSTFLAGS"], "SYSFLAGS": v["SYSFLAGS"], "COUNT": len(self.players), "PRIV": v["PRIV"],
             "MINSIZE": v["MINSIZE"], "MAXSIZE": v["MAXSIZE"], "NUMPART": 1, "PARTSIZE0": v["MAXSIZE"],
             "PARTPARAMS0": "", "SEED": self.seed, "WHEN": self.when, "AUTH": "", "SESS": v["SESS"]}
        for i, p in enumerate(self.players):
            r.update({f"OPPO{i}": p.name, f"OPID{i}": p.ident, f"ADDR{i}": p.address_text,
                      f"LADDR{i}": p.address_text, f"MADDR{i}": p.maddr, f"OPPART{i}": 0, f"OPPARAM{i}": "",
                      f"OPFLAG{i}": 0, f"PRES{i}": 0})
        return r


class Connection:
    def __init__(self, lobby, writer, ident):
        self.lobby = lobby
        self.writer = writer
        self.ident = ident
        self.peer = writer.get_extra_info("peername")
        self.name = ""
        self.persona = ""
        self.maddr = ""
        self.player = None          # the signed-in Player (nfsmw_server.py) this connection belongs to
        self.auxi = ""
        self.room = None
        self.game = None
        self.slots = {}             # USERSET slot -> room name
        self.logged_in = False

    @property
    def address_text(self):
        if self.player is not None:
            a = self.player.address
            return f"{a >> 24}.{(a >> 16) & 255}.{(a >> 8) & 255}.{a & 255}"
        return "0.0.0.0"

    def __repr__(self):
        return self.name or f"lobby connection {self.ident}"

    def send(self, command, fields=None, subcode=b"\0\0\0\0"):
        if log.isEnabledFor(logging.DEBUG) and command != "~png":
            log.debug("%s <- %s%s %s", self, command, "" if subcode == b"\0\0\0\0" else "/" + subcode.decode("latin-1"),
                      " ".join(f"{k}={v}" for k, v in (fields or {}).items())[:400])
        if not self.writer.is_closing():
            self.writer.write(encode(command, fields, subcode))

    def user_record(self):
        return {"I": self.ident, "M": self.name, "N": self.persona or self.name, "MA": self.maddr,
                "A": self.address_text, "LA": self.address_text, "G": self.game.ident if self.game else 0,
                "P": 1, "F": "U", "LO": "enUS", "PRES": 1, "X": self.auxi}

    def member_record(self, slot):
        return {"S": slot, "I": self.ident, "N": self.name, "F": "U",
                "G": self.game.ident if self.game else 0, "X": self.auxi}


class Lobby:
    def __init__(self, server):
        self.server = server            # nfsmw_server.Server (signed-in players)
        self.connections = set()
        self.rooms = {}                 # ident -> Room
        self.games = {}                 # lower name -> Game
        self.counter = 1000
        self.stats = Stats()

    def next_ident(self):
        self.counter += 1
        return self.counter

    # ---- connections -------------------------------------------------------------

    async def handle(self, reader, writer):
        conn = Connection(self, writer, self.next_ident())
        self.connections.add(conn)
        heartbeat = asyncio.ensure_future(self.heartbeat(conn))
        try:
            buffer = b""
            while True:
                data = await reader.read(65536)
                if not data:
                    break
                buffer += data
                if len(buffer) > 4 * MAX_MESSAGE:
                    break
                while len(buffer) >= 12:
                    (length,) = struct.unpack(">I", buffer[8:12])
                    if length < 12 or length > MAX_MESSAGE:
                        log.info("%s: bad message length %d, closing", conn, length)
                        return
                    if len(buffer) < length:
                        break
                    message, buffer = buffer[:length], buffer[length:]
                    command = message[:4].decode("latin-1")
                    try:
                        self.dispatch(conn, command, message[12:])
                    except Exception:
                        log.exception("%s: %s", conn, command)
                        conn.send(command)
                await writer.drain()
        except (ConnectionError, asyncio.IncompleteReadError):
            pass
        finally:
            heartbeat.cancel()
            self.disconnect(conn)
            writer.close()

    async def heartbeat(self, conn):
        while True:
            await asyncio.sleep(HEARTBEAT)
            if conn.logged_in:
                conn.send("~png", {"REF": now_text(), "TIME": 2})
                try:
                    await conn.writer.drain()
                except ConnectionError:
                    return

    def disconnect(self, conn):
        self.connections.discard(conn)
        if conn.game is not None:
            self.leave_game(conn)
        if conn.room is not None:
            self.leave_room(conn)
        for room in [r for r in self.rooms.values() if r.owner is conn]:
            self.delete_room(room)
        if conn.logged_in:
            log.info("%s left the lobby", conn)

    def link_player(self, conn, peer_host):
        """The signed-in player this lobby connection belongs to: the XUID in
        its MADDR ($<xuid>^...), from the same host as the sign-in."""
        xuid = None
        if conn.maddr.startswith("$"):
            try:
                xuid = int(conn.maddr[1:].split("^", 1)[0], 16)
            except ValueError:
                xuid = None
        players = [p for p in self.server.players.values() if p.host == peer_host]
        for p in players:
            if xuid is not None and p.xuid == xuid:
                return p
        for p in players:
            if conn.name and p.name.lower() == conn.name.lower():
                return p
        return None

    # ---- dispatch ------------------------------------------------------------------

    def dispatch(self, conn, command, body):
        f = decode_body(body)
        if log.isEnabledFor(logging.DEBUG) and command != "~png":
            log.debug("%s -> %s %s", conn, command, " ".join(f"{k}={v}" for k, v in f.items())[:400])
        handler = getattr(self, "cmd_" + command.strip("@~+").replace(" ", ""), None)
        if command == "~png":
            return                      # the game's answer to our ping
        if handler is None:
            log.debug("%s: unhandled %r", conn, command)
            conn.send(command)
            return
        handler(conn, f)

    def cmd_tic(self, conn, f):
        pass                            # unanswered: the session stays unencrypted

    def cmd_dir(self, conn, f):
        conn.send("@dir", {"ADDR": SERVICE_ADDRESS, "PORT": LOBBY_PORT,
                           "SESS": 1000000000 + secrets.randbelow(10**9), "MASK": secrets.token_hex(16)})

    def cmd_addr(self, conn, f):
        pass                            # no reply

    def cmd_skey(self, conn, f):
        conn.send("skey", {"SKEY": "$" + "0" * 32})

    def cmd_news(self, conn, f):
        conn.send("news", {"BUDDY_SERVER": SERVICE_ADDRESS, "BUDDY_PORT": 10899,
                           "TOSAC_URL": "", "TOSA_URL": "", "TOS_URL": ""})

    def cmd_auth(self, conn, f):
        conn.name = clean(f.get("NAME") or f.get("GTAG") or "", 32)
        conn.maddr = clean(f.get("MADDR", ""), 512)
        peer_host = conn.peer[0] if conn.peer else None
        conn.player = self.link_player(conn, peer_host)
        if conn.player is None:
            log.info("lobby login from %s as %r refused: not signed in to the server", peer_host, conn.name)
            conn.send("auth", None, b"pass")
            return
        conn.name = conn.player.name     # the name the server knows, whatever was sent
        conn.logged_in = True
        log.info("%s logged in to the lobby", conn)
        conn.send("auth", {"NAME": conn.name, "ADDR": conn.address_text, "PERSONAS": conn.name, "LOC": "enUS",
                           "MAIL": "player@speedbreaker", "SPAM": "NN", "TOS": 1, "SINCE": EPOCH, "LAST": EPOCH})

    def cmd_pers(self, conn, f):
        if not conn.logged_in:
            return conn.send("pers", None, b"auth")
        conn.persona = conn.name
        if f.get("MADDR"):
            conn.maddr = clean(f["MADDR"], 512)
        conn.send("pers", {"PERS": conn.persona, "LKEY": secrets.token_hex(16), "LOC": "enUS",
                           "A": conn.address_text, "LA": conn.address_text, "IDLE": 600000})

    def cmd_sele(self, conn, f):
        conn.send("sele", {k: clean(v) for k, v in f.items()})
        conn.send("+who", conn.user_record())
        for k, v in f.items():
            if not k.startswith("USERSET"):
                continue
            try:
                slot = int(k[7:])
            except ValueError:
                continue
            if not v:
                conn.slots.pop(slot, None)
                continue
            conn.slots[slot] = v
            room = self.room_by_name(v)
            if room is not None:
                conn.send("+ust", room.record())
                for m in room.members:
                    conn.send("+usm", m.member_record(slot))

    def cmd_user(self, conn, f):
        name = clean(f.get("PERS") or conn.name, 32)
        s = self.stats.get(name)
        v = hex_stats(self.stats.vector(name))
        conn.send("user", {"PERS": name, "LAST": EPOCH, "EXPR": 1072566000, "STAT": v, "LWSTAT": hex_stats([0] * STAT_COUNT),
                           "LMSTAT": hex_stats([0] * STAT_COUNT), "LGAME": s["last"]})

    # ---- rooms ------------------------------------------------------------------------

    def room_by_name(self, name):
        for r in self.rooms.values():
            if r.name == name:
                return r
        return None

    def subscribers(self, room):
        """(connection, slot) for every connection subscribed to `room`."""
        for c in self.connections:
            for slot, name in c.slots.items():
                if name == room.name:
                    yield c, slot

    def push_member(self, room, member, remove=False):
        for c, slot in self.subscribers(room):
            c.send("+usm", {"S": slot, "I": member.ident} if remove else member.member_record(slot))

    def cmd_usea(self, conn, f):
        rooms = [r for r in self.rooms.values() if r.owner is not conn]
        conn.send("usea", {"START": clean(f.get("START", "0")), "COUNT": len(rooms)})
        for r in rooms:
            conn.send("+uss", r.record())

    def cmd_ucre(self, conn, f):
        for room in [r for r in self.rooms.values() if r.owner is conn]:
            self.delete_room(room)
        if conn.room is not None:
            self.leave_room(conn)
        room = Room(self.next_ident(), conn, f)
        self.rooms[room.ident] = room
        conn.room = room
        conn.slots[0] = room.name
        log.info("%s opened room %r", conn, room.name)
        conn.send("ucre", room.record())
        conn.send("+usr", conn.user_record())
        conn.send("+usm", conn.member_record(0))

    def cmd_uadm(self, conn, f):
        room = conn.room
        if room is None:
            return conn.send("uadm")
        if "NAME" in f:
            for c, slot in list(self.subscribers(room)):
                c.slots[slot] = clean(f["NAME"], 64)
            room.name = clean(f["NAME"], 64)
        if "DESC" in f:
            room.desc = clean(f["DESC"])
        if "PARAMS" in f:
            room.params = clean(f["PARAMS"], 2048)
        if "CUSTFLAGS" in f:
            room.custflags = clean(f["CUSTFLAGS"])
        conn.send("uadm", room.record())

    def find_room(self, f):
        for key in ("I", "IDENT", "ROOM"):
            try:
                room = self.rooms.get(int(f.get(key, "")))
            except ValueError:
                room = None
            if room is not None:
                return room
        name = f.get("NAME") or f.get("N")
        if name:
            room = self.room_by_name(name)
            if room is not None:
                return room
            for r in self.rooms.values():
                if r.owner.name.lower() == name.lower():
                    return r
        return None

    def cmd_ujoi(self, conn, f):
        room = self.find_room(f)
        if room is None:
            return conn.send("ujoi", None, b"nfnd")
        if conn.room is not None and conn.room is not room:
            self.leave_room(conn)
        others = [m for m in room.members if m is not conn]
        if conn not in room.members:
            room.members.append(conn)
        conn.room = room
        for m in others:
            m.send("+usr", conn.user_record())
        conn.send("ujoi", room.record())
        for m in others:
            conn.send("+usr", m.user_record())
        conn.send("+usr", conn.user_record())
        for c, slot in self.subscribers(room):
            c.send("+usm", conn.member_record(slot))

    def leave_room(self, conn):
        room = conn.room
        conn.room = None
        if room is None:
            return
        if conn in room.members:
            room.members.remove(conn)
        self.push_member(room, conn, remove=True)

    def cmd_ulea(self, conn, f):
        room = conn.room
        conn.send("ulea")
        if room is not None:
            if conn in room.members:
                room.members.remove(conn)
            conn.room = None
            self.push_member(room, conn, remove=True)

    def delete_room(self, room):
        for m in list(room.members):
            if m is not room.owner:
                self.push_member(room, m, remove=True)
                m.room = None
        if room.owner.room is room:
            room.owner.room = None
        self.rooms.pop(room.ident, None)
        log.info("room %r closed", room.name)

    def cmd_udel(self, conn, f):
        room = self.room_by_name(f.get("NAME", "")) if f.get("NAME") else conn.room
        if room is not None and room.owner is conn:
            self.delete_room(room)
        conn.send("udel")

    # ---- games ----------------------------------------------------------------------

    def cmd_gcre(self, conn, f):
        if conn.game is not None:
            self.leave_game(conn)
        game = Game(self.next_ident(), conn, conn.room, f)
        old = self.games.get(game.name.lower())
        if old is not None and old.host is not conn:
            game.name = f"{game.name}-{game.ident}"
        self.games[game.name.lower()] = game
        conn.game = game
        log.info("%s created race %r", conn, game.name)
        conn.send("gcre", game.record(conn))
        conn.send("+who", conn.user_record())

    def cmd_gset(self, conn, f):
        game = conn.game
        if game is None:
            return conn.send("gset")
        game.update(f)
        for p in game.players:
            if p is not conn:
                p.send("+mgm", game.record(p))
        conn.send("gset", game.record(conn))

    def cmd_gjoi(self, conn, f):
        game = self.games.get(f.get("NAME", "").lower())
        if game is None:
            return conn.send("gjoi", None, b"nfnd")
        try:
            limit = int(game.values.get("MAXSIZE", "8"))
        except ValueError:
            limit = 8
        if conn not in game.players and len(game.players) >= max(2, limit):
            return conn.send("gjoi", None, b"full")
        if conn.game is not None and conn.game is not game:
            self.leave_game(conn)
        if conn not in game.players:
            game.players.append(conn)
        conn.game = game
        log.info("%s joined %s's race", conn, game.host)
        for p in game.players:
            if p is not conn:
                p.send("+mgm", game.record(p))
        conn.send("gjoi", game.record(conn))
        conn.send("+who", conn.user_record())

    def cmd_gsta(self, conn, f):
        game = conn.game
        if game is not None:
            log.info("%s started the race (%d players)", conn, len(game.players))
            for p in game.players:
                p.send("+ses", game.record(p))
        conn.send("gsta")

    def leave_game(self, conn):
        game = conn.game
        conn.game = None
        if game is None:
            return
        if conn in game.players:
            game.players.remove(conn)
        if conn is game.host or not game.players:
            self.games.pop(game.name.lower(), None)
            for p in game.players:
                p.game = None
            log.info("race %r closed", game.name)
        else:
            for p in game.players:
                p.send("+mgm", game.record(p))

    def cmd_glea(self, conn, f):
        self.leave_game(conn)
        conn.send("glea")

    def cmd_gdel(self, conn, f):
        self.leave_game(conn)
        conn.send("gdel")

    # ---- messages, state, results ------------------------------------------------------

    def cmd_mesg(self, conn, f):
        text, attr = clean(f.get("TEXT", ""), 512), clean(f.get("ATTR", ""), 32)
        if conn.game is not None:
            targets = [p for p in conn.game.players if p is not conn]
        elif conn.room is not None:
            targets = [m for m in conn.room.members if m is not conn]
        else:
            targets = []
        for t in targets:
            t.send("+msg", {"N": conn.name, "T": text, "F": attr})
        conn.send("mesg")

    def cmd_auxi(self, conn, f):
        conn.auxi = clean(f.get("TEXT", ""), 512)
        conn.send("auxi", {k: clean(v) for k, v in f.items()})
        room = conn.room
        if room is not None:
            for m in room.members:
                if m is not conn:
                    m.send("+usr", conn.user_record())
            conn.send("+usr", conn.user_record())
            for c, slot in self.subscribers(room):
                c.send("+usm", conn.member_record(slot))

    def cmd_rank(self, conn, f):
        try:
            blob = base64.b64decode(urllib.parse.unquote(f.get("RESU", "")))
            if len(blob) >= 8 and blob[0] == 3:
                count, me = blob[1], blob[3]
                at = 8
                for _ in range(count):
                    if at + 51 > len(blob):
                        break
                    index, position = blob[at], blob[at + 1]
                    extra = blob[at + 50]
                    if index == me and 1 <= position <= count:
                        self.stats.record(conn.name, position, count)
                        log.info("%s finished %d of %d", conn, position, count)
                    at += 51 + extra
        except (ValueError, IndexError):
            pass
        conn.send("rank")

    def cmd_snap(self, conn, f):
        board = self.stats.board()
        rows = []
        if f.get("FIND", "").startswith("$"):
            rows = [(i, s) for i, s in enumerate(board) if s["name"].lower() == conn.name.lower()]
        else:
            try:
                start = max(0, int(f.get("START", "0")))
                count = max(0, min(100, int(f.get("RANGE", "100"))))
            except ValueError:
                start, count = 0, 100
            rows = list(enumerate(board))[start:start + count]
        conn.send("snap", {"CHAN": clean(f.get("CHAN", "0")), "START": clean(f.get("START", "0")), "RANGE": len(rows)})
        for i, s in rows:
            conn.send("+snp", {"N": s["name"], "R": i + 1, "P": s["points"], "O": 0,
                               "S": hex_stats([0, s["wins"], s["losses"], 0, 0, 0, s["points"]])})

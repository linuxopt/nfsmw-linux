# SpeedBreaker online server

Online races for Need for Speed: Most Wanted (Xbox 360) in SpeedBreaker builds. The server stands
in for what the game used to reach online: **Xbox Live** (signing in, addresses, passing the race
traffic between players) and **EA's lobby** (logging in, the list of races, starting them).

- `nfsmw_server.py`: sign-in and the race relay. Run this one.
- `nfsmw_lobby.py`: EA's lobby; `nfsmw_server.py` loads it.
- `nfsmw-online.service`: a systemd unit for a Linux host.

Python 3.9 or newer, standard library only: nothing to install.

## Running it

```
python3 nfsmw_server.py [--host 0.0.0.0] [--port 39300] [--motd "Welcome"] [--trace]
```

| Port | Protocol | What |
|---|---|---|
| `--port` (39300) | TCP | sign-in and requests (the game's runtime) |
| `--port` (39300) | UDP | the race relay |
| `--port` + 1 (39301) | TCP | EA's lobby (the game's online menus) |

Open these three on the host's firewall. Players need no open ports: their game only ever
connects out to the server. `--trace` logs every lobby message, for debugging (it logs what
players send, so leave it off otherwise).

On a Linux host with systemd:

```
mkdir -p /opt/nfsmw-online
cp nfsmw_server.py nfsmw_lobby.py /opt/nfsmw-online/
cp nfsmw-online.service /etc/systemd/system/
systemctl daemon-reload
systemctl enable --now nfsmw-online
journalctl -u nfsmw-online -f
```

The unit runs it as a throwaway user with no write access anywhere (`DynamicUser`). Everything is
in memory: restarting the server signs everyone out and clears the leaderboards.

## How a race happens

1. **Sign-in.** When the game boots with online play set up (`online.toml`), it connects to the
   server with the player's name and id. The server gives it an address in `10.80.0.0/16`, which
   is all the other players ever see of it (never its real IP), and a token for the relay.
   Names must be unique among the players online.
2. **Lobby.** In Xbox Live > Quick Match, the game asks EA's directory (`@dir`) where the lobby is
   and is told `10.80.0.1:10901`; the runtime sends every connection to that address to port
   39301. There the game logs in (the lobby only accepts a player signed in from the same IP),
   searches for rooms (`usea`), and creates a room and a race (`ucre`, `gcre`, `gset`) or joins one
   (`sele`, `ujoi`, `gjoi`, `mesg`). The host's `gsta` starts it (`+ses` to everyone).
3. **Racing.** The games talk to each other over UDP port 3658, addressed by their `10.80.x.y`
   addresses. The runtime sends each packet to the server's relay with the destination; the
   server forwards it to that player. A race carries a few KB/s per player.
4. **Results.** Each player reports the race (`rank`); the server keeps points (10 per place
   beaten, plus 10) for the in-game leaderboards (`snap`).

## Protocol

### Sign-in and requests (TCP, `--port`)

Frames: `[u32 length][u8 op][u32 request id][body]`, big-endian; `length` counts op, id and body.
Body fields: `u8/u16/u32/u64`, and `bytes` = `[u32 length][data]` (a string is UTF-8 bytes).

| Op | Request | Reply |
|---|---|---|
| 1 hello | `u32 protocol (1)`, `str name`, `u64 xuid` | `u32 address`, `u32 token`, `str message of the day` |
| 2 ping | | |
| 20 QoS listen | `bytes data` (up to 1 KB, what other players' QoS lookups get) | |
| 21 QoS lookup | `u32 n`, n × `u32 address` | `u32 n`, n × (`u8 found`, `bytes data`) |
| 10-18 sessions | (Live matchmaking: create, modify, delete, search, details, join, leave, start, end; this game finds races through EA's lobby instead and doesn't use them) | |
| 255 error | | `str reason` (in place of any reply) |

The first frame must be hello; a refused hello is answered with an error and the connection closed.

### Relay (UDP, `--port`)

- Keepalive, every 4 s: `[u8 1][u32 token]`. The server answers nothing; it learns where to send
  the player's packets (the player's NAT keeps the path open).
- To the server: `[u8 2][u32 token][u16 from port][u32 to address][u16 to port][payload]`.
- From the server: `[u8 2][u32 from address][u16 from port][u16 to port][payload]`.

Payloads up to 1264 bytes; a sender is limited to 2000 packets a second. A packet with a token
the server doesn't know, or to a player it hasn't heard from in 30 s, is dropped.

### EA's lobby (TCP, `--port` + 1)

EA's 2003-2006 lobby protocol ("Aries"/DirtySock): each message is a 4-character command, a
4-byte subcode (zero, or an error code such as `nfnd`), a big-endian u32 total length (header
included), and `KEY=value` lines ending in a NUL. `@tic` (an offer to encrypt) is never answered,
so everything stays in plain text. The server sends `~png` every 20 s (the game gives up after
60 s of silence). `nfsmw_lobby.py` handles the directory, login (`addr`, `skey`, `news`, `auth`,
`pers`, `sele`, `user`), rooms (`usea`, `ucre`, `uadm`, `ujoi`, `ulea`, `udel`), races (`gcre`,
`gset`, `gjoi`, `gsta`, `glea`, `gdel`), messages and state (`mesg`, `auxi`), results (`rank`) and
leaderboards (`snap`), and answers anything else with an empty reply of the same command. The
records the game reads are described in `nfsmw_lobby.py`.

The protocol as this game uses it was worked out by the
[NFSMW Recompiled](https://github.com/victorgbd/nfsmw-server-lan) project, whose notes this
server follows (it is written separately, not derived from their code).

## Tests

`tests/online_server_test.py` (run by `ctest`, or `python3 -B tests/online_server_test.py`):
sign-in, names, the relay, QoS, and the lobby from login to the start line and the leaderboard.

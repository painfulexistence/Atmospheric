# Netcode — Rooms & CCU Roadmap

Plan for taking the realtime examples from "one process = one match" to
"multiple concurrent matches per process", and what CCU that buys. Written
against the state of the `network-subsystem` branch (DatagramSocket seam,
NetConditioner/NetMetrics, WebTransport→UDP gateway, client hello/silence
watchdog + authority stale-slot reclamation).

## Where things stand

| Server | Shape today | Rooms | Max players |
|---|---|---|---|
| `DeathmatchServer` | one `DeathmatchAuthority` = one 1v1 match, `PlayerSlot _slots[2]`, identity = `(addr, port)` | none | **2** |
| `HideAndSeekServer` | same pattern (1 seeker + 1 hider) | none | 2 |
| `RelayServer` (`UdpRelay`) | dumb datagram forwarder for P2P lockstep | **already roomed**: `roomId → Room`, `maxRooms = 1024`, `maxPeersPerRoom = 8`, per-IP room-creation rate limit, stale-room eviction | 1024 rooms × 8 peers (forwarding only) |
| WT gateway (`gateway/wt_udp_gateway.py`) | 1 WebTransport session ↔ 1 local UDP socket | n/a (transparent) | fd-limit bound |

Two deliberate non-goals, stated up front:

- **No shared "room framework" across relay and authoritative servers.** The
  relay forwards opaque bytes and never inspects them; a Deathmatch room owns
  an authoritative simulation. The only thing they share is the word "room" —
  unifying them would be abstraction for its own sake. The relay is already
  done; it is listed here only so nobody re-plans it.
- **No engine-level RoomSubsystem.** Rooms are gameplay-server structure; they
  live with each example's server, same reasoning as authority.hpp itself.

## Deathmatch rooms — target design

One process, one `DatagramSocket`, N concurrent 1v1 matches. Identity stays
`(addr, port)` end to end — the WT gateway already makes browser clients look
exactly like that, so rooms work identically for native and web clients with
no gateway changes.

```
                      ┌────────────────────────────────────────────┐
   datagrams ────────>│ DeathmatchRoomServer (one DatagramSocket)  │
                      │   ClientKey lookup ──> MatchDirectory      │
                      │        │                    │              │
                      │        v                    v              │
                      │  DeathmatchMatch #1   DeathmatchMatch #2 … │  (per-match sim,
                      │   slots[2], rockets,   …                   │   60 Hz, lag comp)
                      └────────────────────────────────────────────┘
                               snapshots out via the same socket
```

### Class list

| Class | File (planned) | Status | Responsibility |
|---|---|---|---|
| `sim::*` (StepPlayer, RailHits, …) | `sim_common.hpp` | exists, unchanged | Pure deterministic rules shared by client prediction and server authority. Knows nothing of matches, sockets, or rooms. |
| `DeathmatchMatch` | `match.hpp/.cpp` (extracted from `authority.cpp`) | **new (extraction)** | One 1v1 match: the two `PlayerSlot`s, rockets, tick loop, lag-comp position history, respawns, scoring, per-slot input buffers, snapshot building. **Owns no socket**: sends through a per-slot sink (`SendFn`) injected by whoever hosts it. Everything currently in `DeathmatchAuthority` except Bind/Pump/socket. |
| `ClientKey` | `match.hpp` | new (tiny) | `(addr, port)` identity of one client, with equality + hash so it can key an `unordered_map`. The one place sender identity is defined. |
| `MatchDirectory` | `room_server.cpp` | new | Room bookkeeping: `roomId → DeathmatchMatch`, `ClientKey → (roomId, slot)`. Join policy on hello (explicit roomId if the hello carries one; else fill the first match with a free slot, else create). Reaps empty/idle matches. Enforces `maxMatches`. No socket, no sim — routing and lifecycle only. |
| `DeathmatchRoomServer` | `room_server_main.cpp` (new binary) | new | The process: owns the single `DatagramSocket`, drains datagrams, resolves `ClientKey` → match via `MatchDirectory`, forwards packets, pumps every match at its tick rate, and provides each match's send sink (`socket.SendTo(key…)`). Single-threaded. |
| `DeathmatchAuthority` | `authority.hpp/.cpp` | **shrinks** | Becomes the thin "one socket + one `DeathmatchMatch`" composition it already is conceptually. Keeps its name and API so `--local`, the listen-server shape, and `DeathmatchServer` (the teaching 1v1 binary, kept as-is) don't change. |
| `proto` additions | `protocol.hpp` | small change | `ClientHello` gains an optional roomId (0 = matchmake me); a protocol version byte so old clients are rejected loudly instead of misparsed. Snapshot format unchanged. |

Client side needs nothing new — a client already only knows "the server"; which
match it is in is the server's business. (`--connect ip port` optionally grows
`--room <id>` to fill the hello field.)

### Phases, each independently verifiable

1. **Extract `DeathmatchMatch`** out of `DeathmatchAuthority` — pure refactor,
   behavior-identical, `DeathmatchServer`/`--local` untouched. Verified by the
   existing headless client/authority loopback exercises (reconciliation
   residual = 0, lag-comp hit/miss, shield block) run before/after.
2. **`MatchDirectory` + `DeathmatchRoomServer` + hello roomId.** Verified
   headlessly: script M clients across K rooms over loopback UDP, assert
   isolation (a rail fired in room 1 never damages room 2), join policy, and
   slot/room reaping via the existing 8 s timeout.
3. **Perf levers, only if a real target demands them** (measure first):
   snapshot rate 60 → 20-30 Hz (halves egress pps), `recvmmsg` batching,
   input redundancy. Not before there is a measured need — YAGNI.
4. **Multi-process orchestration** (sketch only, out of scope): one process
   per N rooms behind a matchmaker that hands out `host:port:roomId`. This is
   infra (Agones-style), not engine code, and phase 2 CCU makes it far away.

### CCU envelope (honest estimates, not promises)

Per-match state is tiny (~10 KB dominated by 2 × 256-tick position history);
per-tick sim cost is microseconds (two capsules, a handful of rockets, no
physics engine). The real single-process bound is **packets per second**, not
simulation:

- Per client: 60 input pps in; snapshots out at tick rate (60 today).
- 500 matches = 1000 CCU ⇒ ~60k pps in + 60k pps out on one socket/thread.
  Feasible on one modern core, but syscall-bound — this is exactly what the
  phase-3 levers (snapshot at 20-30 Hz, `recvmmsg`) buy back.
- Rule of thumb for this codebase: **single process, single thread ≈ high
  hundreds of matches (≈ 1-2k CCU)** before phase 3 matters, more after.
  Anything beyond that is phase 4 (more processes), not more threads —
  per-match sharding across processes is simpler and matches deployment
  reality.

Gateway side (web clients): one QUIC session + one UDP socket per browser
client — raise `ulimit -n`, and QUIC crypto CPU means gateways scale
horizontally (they're stateless per session; run several).

## MultiplayerSandbox relay — reviewed, nothing to build

`UdpRelay` already has everything a forwarding relay needs for rooms/CCU:
roomId map (1024), 8 peers/room, per-source-IP creation rate limiting, stale
eviction. Its CCU cost is pure forwarding pps (no sim, no history). The only
worthwhile addition, when observability is wanted: expose room/peer counts +
forwarded-pps as `NetMetrics`-style counters (it's dep-free, so a status log
line, not a HUD). Explicitly **not** planned: sharing room code with
Deathmatch (see non-goals).

## HideAndSeek

Follows Deathmatch's extraction pattern (`HideAndSeekMatch`, same directory /
room-server shape) once Deathmatch's phase 2 proves it. Not planned in detail
— doing it second is the point: the second consumer validates the shape.

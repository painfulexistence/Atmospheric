# Netcode in Atmospheric

A tour of the multiplayer stack: three complete, contrasting network
architectures, the shared transport primitives beneath them, and the design
decisions behind each. Nothing here is a library dependency — the socket layer,
the reliability layer, the link emulator, and each game's authority/prediction
are all in-repo and headless-tested.

The guiding principle is that **there is no single "netcode" — the right model is
a function of the genre.** So the repo ships three, side by side, and the
interesting content is *why each chose what it did.*

## The three architectures

| Example | Model | Reconciliation | Who is authoritative | Genre it fits |
|---|---|---|---|---|
| **MultiplayerSandbox** | Peer-to-peer **deterministic lockstep**, delay-based | none needed — every peer runs the identical sim | all peers equally (no server) | RTS, deterministic co-op, falling-sand |
| **HideAndSeek** | Client/server **state sync**, error-smoothing | blend toward the server each snapshot; hard-snap on large divergence | dedicated (or listen) server | casual PvP where frame-perfect precision isn't required |
| **Deathmatch** | Client/server **state sync**, exact rewind-replay + server lag compensation | reset to the server's authoritative state, replay unacked inputs (zero residual) | dedicated (or listen) server | competitive FPS |

### Why lockstep for the sandbox — and why *not* for the shooter

Lockstep exchanges **inputs only**, never state: all peers run the same
deterministic simulation from the same seed, so a few bytes of input per tick
reproduce a whole world identically on every machine
(`Examples/MultiplayerSandbox/net_lockstep.hpp`). It is bandwidth-tiny and
cheat-resistant to *state* forgery (there is no authoritative state to forge),
and it detects divergence with periodic checksums (`ShareChecksum`).

Its costs are exactly why the shooter rejects it: every input must arrive before
the sim can advance, so it is **delay-based** — a laggy peer stalls everyone —
and it demands bit-exact determinism across platforms, which floating-point
physics fights. A twitch FPS instead wants each player to move *now*, locally,
and let the server sort out truth — which is client-side prediction, below.

## Client-side prediction & reconciliation (Deathmatch)

The problem: on a server-authoritative game, if the client waited for the
server to confirm every move, control would lag by a full round trip. So the
client **predicts** — it applies its own input immediately with the same
`sim::StepPlayer` rules the server uses — and later **reconciles** the guess
against the server's authoritative answer.

Two reconciliation strategies are implemented, deliberately:

- **Exact rewind-replay** (`Examples/Deathmatch/client_net.cpp`, `HandleSnapshot`).
  Each snapshot acks the last input the server consumed. The client resets its
  predicted state to the server's authoritative state *as of that input*, then
  **replays every still-unacked input** on top. Because movement is
  deterministic and the server consumes each input exactly once in order
  (`Examples/Deathmatch/authority.cpp`), the replay reproduces the client's
  prediction bit-for-bit — **zero residual on a clean link.** The netgraph's
  `predErr` reads ~0 until real divergence (a hit, a respawn) occurs.

- **Error-smoothing** (`Examples/HideAndSeek/client_net.cpp`). Rather than a
  precise input history, the client blends a fraction of the server/predicted
  difference in each snapshot and hard-snaps only past a large threshold. Less
  code, a little more visible correction lag — a legitimate simplification for a
  game with no frame-perfect requirement. The contrast is the point.

The remote player is never predicted — it is **interpolated** between the last
two snapshots with a fixed render delay (position and, in 3D, orientation via
shortest-arc yaw). That render delay is also what the server rewinds to for lag
compensation.

## Server-side lag compensation (Deathmatch)

`Examples/Deathmatch/authority.hpp` keeps 256 ticks (~4.3 s) of each player's
position history. When a shot arrives, the server **rewinds** the target to
where it was on the *shooter's* screen (the tick the shooter's client tagged the
shot with) and tests the hit there — "favor the shooter": if your crosshair was
on them, you hit, even though by server-present they'd moved.

Two weapons show the tradeoff explicitly:

- **Railgun (hitscan):** point-in-time rewind — one tick, the instant of firing.
- **Rocket (projectile):** rolling rewind with a `favorShooterRockets` toggle.
  Favor-shooter judges the rocket against the target as it was `lagTicks` in the
  past at every step of flight ("I died behind cover"); favor-target judges it
  against the present, letting the victim dodge what they see. There is no
  universally correct answer — it's a design dial, so the code exposes it
  (`DeathmatchServer --favor-target`).

## The transport stack

Written against a single seam so the same netcode runs everywhere:

- **`DatagramSocket`** (`Engine/include/Atmospheric/datagram_socket.hpp`) — a
  compile-time alias, *not* a virtual interface (a binary only ever uses one
  transport, so no per-datagram vtable). It resolves to:
  - **`UdpSocket`** natively — the minimal non-blocking UDP primitive
    (POSIX/Winsock2 shim, ephemeral bind, send/recv).
  - **`LoopbackDatagramSocket`** on the web for `--local` single-player: an
    in-process port registry, no OS sockets, since the browser has no raw UDP.
  - **`WebTransportSocket`** (WIP) for browser PvP: WebTransport (QUIC datagram
    mode) via EM_JS glue.
- **WebTransport→UDP gateway** (`Examples/Deathmatch/gateway/`) — terminates
  QUIC/TLS and forwards datagrams 1:1, giving each browser session its own UDP
  socket so the **dedicated server needs zero changes** to accept web clients.
  End-to-end tested with `aioquic`.
- **`UdpRelay` / `UdpRelayClient`** — a room-based datagram forwarder for P2P
  lockstep behind NAT (roomId map, per-source-IP rate limiting, stale-room
  eviction).
- **`ReliableChannel`** (`Engine/include/Atmospheric/reliable_channel.hpp`) —
  sequencing + ack-bitfield + reliable-ordered messages *on top of* the
  unreliable datagram layer, for must-arrive events. See below.

### Reliable vs unreliable — the decision, not just the mechanism

The realtime paths (snapshots, inputs) are deliberately **unreliable**: a lost
snapshot is superseded by the next one, so resending it would only add latency;
lost inputs are covered by redundant resend (lockstep) or reconciliation
(Deathmatch). Reliability is reserved for messages where *exactly-once, in-order*
actually matters — join/leave, score, match-end, chat — which is what
`ReliableChannel` provides:

- 16-bit packet sequence, plus an `ack` and a 32-bit `ack_bits` — one packet
  acknowledges up to **33** of the peer's packets, so acks survive loss.
- Reliable messages carry a 16-bit id and ride *every* outgoing packet until an
  ack confirms them (redundancy, not a retransmit timer). The receiver dedupes
  by id and releases in order, buffering early arrivals.
- Wrap-safe `(int16_t)(a - b)` serial comparison throughout.

Knowing *when not to use* the reliable channel is as much the point as having it.

## Observability

- **`NetConditioner`** — an inbound-datagram link emulator (latency, jitter,
  loss, duplication) so the netcode runs under adverse conditions instead of a
  0 ms/0 %-loss loopback. Deterministic per seed — the reliability and
  reconciliation tests run *through* it.
- **`NetMetrics` + `DrawNetHud`** — RTT (from input/ack latency), inbound loss,
  bandwidth, prediction error, pending inputs, on-screen and dialable live
  (number keys) in every realtime example.

## Cross-platform reach

Native (Windows/macOS/Linux) and the browser via Emscripten. Lockstep uses a
WebRTC DataChannel for browser P2P; the server-authoritative examples run
`--local` in-browser today and reach a dedicated server via the WebTransport
gateway. See `docs/pvp-testing` for the deployment/testing runbook and
`docs/netcode-rooms-ccu.md` for the rooms/CCU roadmap.

## How it's verified

The socket/reliability/conditioner primitives are dependency-free and covered by
standalone headless tests (loss/reorder/dup for `ReliableChannel`, reconciliation
residual / lag-comp hit-miss / shield-block for the authorities, the gateway's
forwarding path with `aioquic`). The windowed render layer sits on top of that
verified core.

## Interview-question index

| Question | Where in the code |
|---|---|
| How do you handle packet loss & reordering? | `reliable_channel.cpp` (ack-bitfield, redundant resend, ordered release + dedupe); unreliable paths absorb loss via next-snapshot / reconciliation |
| What's your reliability-layer design? | `reliable_channel.hpp` header comment + the reliable-vs-unreliable section above |
| Server-authoritative reconciliation — how? | `Deathmatch/client_net.cpp::HandleSnapshot` (exact rewind-replay) vs `HideAndSeek/client_net.cpp` (error-smoothing) |
| Lag compensation? | `Deathmatch/authority.cpp` position history + `FireRail`/`SpawnRocket`, favor-shooter/target |
| Lockstep vs state sync? | `MultiplayerSandbox/net_lockstep.hpp` vs the two authorities; the "why lockstep / why not" section |
| Why UDP over TCP; how do you rebuild reliability? | reliable-vs-unreliable section + `ReliableChannel` |
| How does WebTransport reach a UDP server? | `Examples/Deathmatch/gateway/README.md` |

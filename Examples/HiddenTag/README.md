# HiddenTag

A server-authoritative 1 Seeker vs 1 Hider hide-and-tag game — the
architecture asymmetric, hidden-information PvP needs, in contrast to
[MultiplayerSandbox](../MultiplayerSandbox)'s peer-symmetric deterministic
lockstep. See `server_main.cpp`'s header comment for exactly why lockstep
doesn't fit this genre (short version: lockstep replicates the *entire* game
state onto every peer, so there is no way to keep one player's position
secret from the other; a server-authoritative model can simply never send it).

## Running

```sh
./HiddenTagServer [--port <n>]                       # default 9100

./HiddenTagClient --role seeker --connect <ip> [port]
./HiddenTagClient --role hider  --connect <ip> [port]
```

Controls: WASD / arrows move, Esc quit. The Seeker sees a faint circle
showing their vision radius; the Hider is only sent to the Seeker's client
when inside it. The Hider always sees the Seeker (asymmetric information —
not "both sides have partial info about each other", genuinely one-directional).

## What this demonstrates that lockstep can't

1. **Hidden information is never sent, not rendered-and-hidden.** The
   server's snapshot to the Seeker simply omits the Hider entirely when out
   of vision — there's nothing in the Seeker's process memory to read out
   with a cheat tool, because the bytes were never transmitted.
2. **The server owns movement speed.** `ClientInput` carries only a
   direction; the server (`sim::Step`) multiplies by its own fixed speed and
   tick delta. A modified client reporting an inflated direction gains
   nothing — direction is clamped to unit length before scaling.
3. **No cross-machine determinism is required.** The server is the sole
   simulator, so unlike lockstep there's no risk of Bullet/Box2D-style
   platform floating-point divergence causing a desync — see the
   [engine review notes](../MultiplayerSandbox) that flagged this as a real
   risk for physics-heavy lockstep games.
4. **Prediction + reconciliation + interpolation**, the client-side
   counterpart to server authority: `ClientNet` predicts the local player's
   movement immediately (via the same `sim::Step` the server uses) for
   responsiveness, corrects it against the server's authoritative position
   as snapshots arrive, and interpolates the other entity (when visible)
   between the last two received samples since its snapshots arrive slower
   than the render rate and there's no local input to predict it from.

## A deliberate simplification

Property 4's reconciliation is **error-smoothing**, not the textbook
per-tick rewind-replay: rather than keeping a precise input history and
replaying it against the server's authoritative position at the exact tick
it corresponds to, `ClientNet::HandleSnapshot` blends a fraction of the
current difference back in every snapshot, hard-snapping only past a large
divergence threshold (see `client_net.cpp`). This trades a little correction
lag for a lot less bookkeeping, which is a fine trade for a game with no
frame-perfect precision requirements. A competitive shooter would want exact
rewind-replay instead.

## Why no relay here

[UdpRelay](../RelayServer) solves NAT traversal between two peers who might
both be behind home routers. In a client-server architecture the server is
already expected to be the one publicly reachable endpoint, so there's
normally nothing for a relay to solve — `UdpRelayClient` remains reusable
here in principle (it only needs a socket handle to send through, and
doesn't care what protocol it's carrying), it just isn't wired into this
example because the scenario it exists for doesn't apply.

## Known limitations (v1)

- No obstacles / line-of-sight occlusion — visibility is distance-only.
- No reconnect handling beyond re-registering a slot on a fresh `ClientHello`
  (a slot can be silently hijacked by anyone who sends one — fine for a demo,
  not for anything public-facing; see `UdpRelay`'s stale-peer rebind logic
  for the kind of check a hardened version would want here too).
- Native only; no Emscripten/web build.

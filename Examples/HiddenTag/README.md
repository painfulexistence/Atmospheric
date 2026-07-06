# HiddenTag

A server-authoritative 1 Seeker vs 1 Hider hide-and-tag game — the
architecture asymmetric, hidden-information PvP needs, in contrast to
[MultiplayerSandbox](../MultiplayerSandbox)'s peer-symmetric deterministic
lockstep. See `authority.hpp`'s header comment for exactly why lockstep
doesn't fit this genre (short version: lockstep replicates the *entire* game
state onto every peer, so there is no way to keep one player's position
secret from the other; a server-authoritative model can simply never send it).

## Running

Two ways to host, one way to join — `HiddenTagClient` connects to either
identically, since the protocol doesn't distinguish deployment shape:

```sh
# Dedicated: standalone process, no attached player
./HiddenTagDedicatedServer [--port <n>]                       # default 9100

# Listen server: one player's own process hosts *and* plays
./HiddenTagListenServer --role seeker [--port <n>]
# (the other player joins as HiddenTagClient below, same as against a dedicated server)

./HiddenTagClient --role seeker --connect <ip> [port]
./HiddenTagClient --role hider  --connect <ip> [port]
```

Controls: WASD / arrows move, Esc quit. The Seeker sees a faint circle
showing their vision radius; the Hider is only sent to the Seeker's client
when inside it. The Hider always sees the Seeker (asymmetric information —
not "both sides have partial info about each other", genuinely one-directional).

## Dedicated server vs. listen server

Both wrap the same `HiddenTagAuthority` (`authority.hpp`/`.cpp`) — the
authoritative simulation + networking core — so there is exactly one
implementation of "who's allowed to know what" to keep correct, not two:

- **`HiddenTagDedicatedServer`** (`dedicated_server_main.cpp`): a plain loop,
  not an `Application` — no GameObject/Scene/rendering needs, so the
  `AddSubsystem<T>`/headless `Application` machinery would only add an
  unproven dependency (same reasoning as [RelayServer vs.
  RelayServerApp](../RelayServer)).
- **`HiddenTagListenServer`** (`listen_server_main.cpp`): a windowed
  `Application` that embeds `HiddenTagAuthority` *and* plays as a normal
  client against it. Its own local player talks to the embedded authority
  exactly like a remote client would — over loopback UDP via an ordinary
  `ClientNet` connected to `127.0.0.1` — rather than a special zero-latency
  path for the host. That's a deliberate simplicity tradeoff: the host pays
  a trivial bit of loopback round-trip latency a "real" listen-server
  implementation would usually special-case away, in exchange for the host
  exercising the exact same prediction/reconciliation code path as everyone
  else (one less thing to get subtly wrong twice).

`HiddenTagClient` and the rendering it shares with `HiddenTagListenServer`
(`render_view.hpp`) have no idea which kind of authority is on the other end
of the socket — that not-knowing is the point: authority (who decides what's
true) is a simulation-layer concern, deployment (standalone process vs.
embedded in a player's game) is a separate choice layered on top.

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
both be behind home routers. A dedicated server is already expected to be
publicly reachable, so there's normally nothing for a relay to solve there —
but a listen server's host is typically an ordinary player behind a home
router, which is exactly `UdpRelay`'s scenario. It isn't wired in here to
keep this example focused, but `UdpRelayClient` would slot in without
changing the protocol (it only needs a socket handle to send through, and
doesn't care what bytes it's carrying) — the one real limitation is that
`UdpRelay`'s rooms are hardcoded to exactly 2 peer slots, which happens to
match this game's 2 players today but wouldn't scale to a listen server with
more than one guest.

## Known limitations (v1)

- No obstacles / line-of-sight occlusion — visibility is distance-only.
- No reconnect handling beyond re-registering a slot on a fresh `ClientHello`
  (a slot can be silently hijacked by anyone who sends one — fine for a demo,
  not for anything public-facing; see `UdpRelay`'s stale-peer rebind logic
  for the kind of check a hardened version would want here too).
- Native only; no Emscripten/web build.

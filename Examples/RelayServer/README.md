# RelayServer

A headless UDP relay for 2-player sessions — the deployment shape of the
engine's `UdpRelay` class. When direct peer-to-peer NAT traversal fails, both
players connect to this relay instead and it forwards their lockstep packets.

```sh
./RelayServer                # relay on UDP :9000
./RelayServer --port 7000    # custom port
```

## How it works

Clients prepend a 4-byte little-endian room id to every datagram
(`LockstepNet` does this automatically in relay mode). The relay:

- creates a room on the first packet for an unseen room id (up to `maxRooms`)
- registers the first two distinct sender addresses as the room's peers
- strips the room id header and forwards the bare payload to the other peer
- lets a new address replace a peer that has been silent for 5 s
  (`kPeerStaleMs`) — handles mobile clients rebinding after a network switch
- evicts rooms idle for 60 s (`kRoomTimeoutMs`)
- caps new-room creation to 5 per source address per minute
  (`maxNewRoomsPerIpPerWindow` / `rateLimitWindowMs`) — throttles one address
  from burning through the whole room budget; packets to an already-open room
  (ordinary gameplay traffic) are never rate-limited

The relay never inspects the payload, so it works with any protocol that
adopts the room-id framing, not just `LockstepNet`.

None of this defends against a distributed flood from many source addresses
at once — that needs network-layer mitigation (firewall the port to known
ranges, lean on your VPS provider's DDoS protection, or put a UDP-aware proxy
like Cloudflare Spectrum in front). Most VPS providers don't filter anything
on a port you open yourself by default.

## Testing against MultiplayerSandbox

`LockstepNet` in the MultiplayerSandbox example has relay support via
`StartRelayHost()` / `StartRelayClient()`. Two ways to exercise it:

- **Automated**: `RelayLoopbackTest` (run via `ctest`) spins up a `UdpRelay`
  and two lockstep peers inside one process over 127.0.0.1 — proves protocol
  correctness (handshake, input exchange, stale-peer rebind), but never
  launches the actual game.
- **Manual, real processes**: run this relay, then two instances of
  `NoitaLikeDemo` with `--relay-host` / `--relay-join` (see
  [MultiplayerSandbox's README](../MultiplayerSandbox/README.md#playing-through-a-relay)).
  Start with all three on one machine over `127.0.0.1` to prove the plumbing,
  then repeat with the relay on a publicly reachable host and the two game
  instances on separate networks to prove it actually solves NAT traversal.

## Notes

`UdpRelay` is a plain class, not an engine Subsystem — it has no per-frame
`Application` dependency and no per-entity meaning, so this example just
`Start()`s it and pumps `Process(dt)` in a manual loop. If a project ever
needs to embed it inside an `Application`-driven process (e.g. to show a
debug panel), the right shape is a thin Subsystem that owns a `UdpRelay`
member and forwards `Process()`/`DrawImGui()` to it — the same pattern
`NetworkSubsystem` uses to wrap `HttpClient`/`WebSocketClient`.

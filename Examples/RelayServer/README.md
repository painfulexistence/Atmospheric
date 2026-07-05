# RelayServer

A headless UDP relay for 2-player sessions — the deployment shape of the
engine's `UdpRelayServer` subsystem. When direct peer-to-peer NAT traversal
fails, both players connect to this relay instead and it forwards their
lockstep packets.

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

The relay never inspects the payload, so it works with any protocol that
adopts the room-id framing, not just `LockstepNet`.

## Testing against MultiplayerSandbox

`LockstepNet` in the MultiplayerSandbox example has relay support via
`StartRelayHost()` / `StartRelayClient()`. An automated end-to-end check
lives in `RelayLoopbackTest` (run via `ctest`), which spins up this relay
subsystem and two lockstep peers inside one process over 127.0.0.1.

## Notes

This example drives the subsystem with a plain fixed-rate loop instead of
`Application`, which currently always creates a window. Once the engine gains
a headless application mode this becomes `app->AddSubsystem<UdpRelayServer>()`.

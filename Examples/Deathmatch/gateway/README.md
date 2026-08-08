# WebTransport → UDP gateway

Lets a **browser** DeathmatchClient play PvP against the unchanged, UDP-only
`DeathmatchServer`:

```
browser ──WebTransport (QUIC datagrams, TLS)──> gateway ──raw UDP──> DeathmatchServer
```

Browsers have no raw UDP; WebTransport datagrams are the browser's UDP-shaped
primitive. Rather than teaching the dependency-free game server QUIC, this
gateway terminates WebTransport and forwards datagrams 1:1. **Each WebTransport
session gets its own local UDP socket**, so the game server sees every browser
client as an ordinary `(addr, port)` — identical to a native client, zero
server changes. Datagram semantics are preserved: no retransmit, no ordering.

## Run

```sh
pip install aioquic

# game server (any machine the gateway can reach; here: same host)
./DeathmatchServer --port 9200

# gateway
python3 wt_udp_gateway.py --cert fullchain.pem --key privkey.pem \
    --port 4433 --path /dm --udp-host 127.0.0.1 --udp-port 9200
```

Then in the web client's Multiplayer card: `https://your.domain:4433/dm`.

## TLS

Browsers only accept WebTransport over a certificate they trust:

- **With a domain**: Let's Encrypt `fullchain.pem` / `privkey.pem` work as-is
  (`certbot certonly --standalone -d your.domain`). Remember renewal.
- **Without one** (LAN testing): a self-signed cert requires launching the page
  with `serverCertificateHashes` support — see the note in
  `Engine/src/web_transport_socket.cpp`; simplest is to just use a domain.

The QUIC port (default 4433/UDP) must be reachable — open it in your firewall.

## Verified

The forwarding path is covered by an end-to-end test (aioquic WebTransport
client ↔ this gateway ↔ a scripted UDP echo server): session accept on the
configured path, 404 elsewhere, datagrams both directions, and two concurrent
sessions appearing at the UDP side as two distinct source ports. The remaining
untested leg is a real browser's WebTransport stack against it.

## Notes

- One gateway can front any UDP game server, not just Deathmatch — nothing in
  it is game-specific. (HideAndSeek would work the same way.)
- Scale limits: one UDP socket + one QUIC session per browser client; raise
  `ulimit -n` for many clients. QUIC crypto costs CPU; run more gateways (or
  move players' gateway close to the game server) as needed.
- A vanished browser (closed tab) leaves its UDP socket until the QUIC
  connection times out; the game server independently reclaims the slot via
  its own 8 s silence timeout (authority.cpp).

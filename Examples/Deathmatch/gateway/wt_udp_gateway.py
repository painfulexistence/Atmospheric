#!/usr/bin/env python3
"""WebTransport -> UDP gateway for DeathmatchServer.

Browsers have no raw UDP, so a web DeathmatchClient reaches the (unchanged,
dependency-free, UDP-only) game server through this gateway:

    browser --WebTransport(QUIC datagrams)--> gateway --raw UDP--> DeathmatchServer

Each WebTransport session gets its OWN local UDP socket, so from the game
server's point of view every browser client is just a distinct (addr, port) —
exactly like a native client. The server needs zero changes; all QUIC/TLS
complexity is quarantined here. Datagram semantics are preserved end to end:
no retransmits, no ordering — losing one loses one, which is the point.

Usage:
    pip install aioquic
    python3 wt_udp_gateway.py --cert fullchain.pem --key privkey.pem \
        [--port 4433] [--path /dm] [--udp-host 127.0.0.1] [--udp-port 9200]

The browser then connects to  https://<your.domain>:4433/dm .
TLS: browsers only accept WebTransport over a trusted certificate — with a
domain, Let's Encrypt certs (fullchain/privkey) work as-is. See README.md.
"""

import argparse
import asyncio
import logging

from aioquic.asyncio import QuicConnectionProtocol, serve
from aioquic.h3.connection import H3_ALPN, H3Connection
from aioquic.h3.events import DatagramReceived, H3Event, HeadersReceived
from aioquic.quic.configuration import QuicConfiguration
from aioquic.quic.events import ConnectionTerminated, ProtocolNegotiated, QuicEvent, StreamReset

log = logging.getLogger("wt-udp-gateway")


class UdpBridge(asyncio.DatagramProtocol):
    """One per WebTransport session: owns the UDP socket whose (addr, port) is
    this browser client's identity at the game server."""

    def __init__(self, gateway: "GatewayProtocol", session_id: int):
        self._gateway = gateway
        self._session_id = session_id
        self.transport = None

    def connection_made(self, transport):
        self.transport = transport

    def datagram_received(self, data, addr):
        # Game server -> browser.
        self._gateway.send_to_session(self._session_id, data)

    def error_received(self, exc):
        log.warning("session %d: UDP error: %s", self._session_id, exc)

    def close(self):
        if self.transport is not None:
            self.transport.close()
            self.transport = None


class GatewayProtocol(QuicConnectionProtocol):
    """One QUIC connection (= one browser tab). It may host WebTransport
    sessions (one per CONNECT); each maps to one UdpBridge."""

    def __init__(self, *args, udp_target=None, wt_path=b"/dm", **kwargs):
        super().__init__(*args, **kwargs)
        self._http = None
        self._bridges = {}  # CONNECT stream_id -> UdpBridge
        self._udp_target = udp_target
        self._wt_path = wt_path

    def quic_event_received(self, event: QuicEvent):
        if isinstance(event, ProtocolNegotiated) and event.alpn_protocol in H3_ALPN:
            self._http = H3Connection(self._quic, enable_webtransport=True)
        elif isinstance(event, ConnectionTerminated):
            for bridge in self._bridges.values():
                bridge.close()
            self._bridges.clear()
        elif isinstance(event, StreamReset) and event.stream_id in self._bridges:
            self._bridges.pop(event.stream_id).close()
            log.info("session %d closed (stream reset)", event.stream_id)
        if self._http is not None:
            for h3_event in self._http.handle_event(event):
                self._h3_event_received(h3_event)

    def _h3_event_received(self, event: H3Event):
        if isinstance(event, HeadersReceived):
            headers = dict(event.headers)
            if (
                headers.get(b":method") == b"CONNECT"
                and headers.get(b":protocol") == b"webtransport"
                and headers.get(b":path") == self._wt_path
            ):
                self._http.send_headers(event.stream_id, [(b":status", b"200")])
                asyncio.ensure_future(self._open_bridge(event.stream_id))
            else:
                self._http.send_headers(event.stream_id, [(b":status", b"404")], end_stream=True)
            self.transmit()
        elif isinstance(event, DatagramReceived):
            # Browser -> game server. stream_id is the session's CONNECT stream.
            bridge = self._bridges.get(event.stream_id)
            if bridge is not None and bridge.transport is not None:
                bridge.transport.sendto(event.data)
            # else: the session's UDP socket isn't up yet — drop. The client's
            # hello retry (client_net.hpp) recovers anything lost here.

    async def _open_bridge(self, session_id: int):
        loop = asyncio.get_event_loop()
        _, bridge = await loop.create_datagram_endpoint(
            lambda: UdpBridge(self, session_id), remote_addr=self._udp_target
        )
        self._bridges[session_id] = bridge
        local = bridge.transport.get_extra_info("sockname")
        log.info("session %d -> udp %s:%d (as %s:%d)", session_id, *self._udp_target, *local[:2])

    def send_to_session(self, session_id: int, data: bytes):
        if self._http is None or session_id not in self._bridges:
            return
        self._http.send_datagram(session_id, data)
        self.transmit()


async def run(args):
    configuration = QuicConfiguration(
        alpn_protocols=H3_ALPN,
        is_client=False,
        # Required for QUIC datagram support to be negotiated at all.
        max_datagram_frame_size=65536,
    )
    configuration.load_cert_chain(args.cert, args.key)

    udp_target = (args.udp_host, args.udp_port)
    wt_path = args.path.encode()
    await serve(
        args.listen,
        args.port,
        configuration=configuration,
        create_protocol=lambda *a, **k: GatewayProtocol(*a, udp_target=udp_target, wt_path=wt_path, **k),
    )
    log.info(
        "listening on https://%s:%d%s -> udp %s:%d", args.listen, args.port, args.path, args.udp_host, args.udp_port
    )
    await asyncio.Event().wait()  # run forever


def main():
    parser = argparse.ArgumentParser(description="WebTransport -> UDP gateway for DeathmatchServer")
    parser.add_argument("--listen", default="0.0.0.0", help="address to listen on (default 0.0.0.0)")
    parser.add_argument("--port", type=int, default=4433, help="UDP port for QUIC (default 4433)")
    parser.add_argument("--cert", required=True, help="TLS certificate chain (PEM), e.g. Let's Encrypt fullchain.pem")
    parser.add_argument("--key", required=True, help="TLS private key (PEM), e.g. Let's Encrypt privkey.pem")
    parser.add_argument("--path", default="/dm", help="WebTransport CONNECT path to accept (default /dm)")
    parser.add_argument("--udp-host", default="127.0.0.1", help="game server address (default 127.0.0.1)")
    parser.add_argument("--udp-port", type=int, default=9200, help="game server UDP port (default 9200)")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()

    logging.basicConfig(level=logging.DEBUG if args.verbose else logging.INFO, format="%(asctime)s %(message)s")
    try:
        asyncio.run(run(args))
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()

#pragma once

// DatagramSocket — the realtime transport seam: an unreliable, message-oriented
// (datagram) send/recv endpoint. It is what the client netcode (ClientNet,
// LockstepNet) is written against, so the same code can run over raw UDP
// natively and, later, over WebTransport in the browser.
//
// It is a *compile-time* alias, not a virtual interface: a binary only ever
// uses one transport (chosen by platform), so there is no reason to pay for
// runtime dispatch on every datagram — the concrete types just share one method
// surface. The reliable/stream side of networking (HTTP, WebSocket) is a
// separate concern; see NetworkSubsystem.
//
// Contract a concrete DatagramSocket provides (matching UdpSocket today):
//   bool     Open(uint16_t port = 0);   // bind (0 = ephemeral)
//   void     Close();
//   void     SendTo(uint32_t addr, uint16_t port, const uint8_t* data, int len);
//   int      RecvFrom(uint8_t* buf, int maxLen, uint32_t& fromAddr, uint16_t& fromPort);
//   bool     IsOpen() const;
//   uint16_t BoundPort() const;
//
// KNOWN BOUNDARIES (real work for the WebTransport impl, not yet solved here):
//   - This surface is connectionLESS (SendTo carries addr/port per call), but
//     WebTransport is connection-ORIENTED (connect to a URL once, then send
//     bytes). A client only ever talks to one peer, so a WebTransportSocket can
//     bridge this with a connected mode — but the contract may need to evolve.
//   - Address resolution stays transport-specific and is NOT part of the seam:
//     UDP resolves ip:port (UdpSocket::Resolve); WebTransport uses a URL.

#ifndef __EMSCRIPTEN__
#include "udp_socket.hpp"
using DatagramSocket = UdpSocket;// native: raw UDP
#else
#include "loopback_datagram_socket.hpp"
// Web has no raw UDP. --local single-player is genuinely one process (a client
// plus an embedded authority), so its "network" is pure in-process loopback —
// no browser networking API needed. Real browser client<->server play would add
// a WebTransportSocket (over QUIC) and select between the two; that's separate,
// later work — see this header's KNOWN BOUNDARIES.
using DatagramSocket = LoopbackDatagramSocket;
#endif

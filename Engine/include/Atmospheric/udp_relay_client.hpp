#pragma once
#include "udp_socket.hpp"
#include <cstdint>
#include <string>

// UdpRelayClient
//
// Client-side counterpart to UdpRelay: wraps outgoing datagrams with the
// [roomId: uint32_t LE] header UdpRelay expects, so any UDP-based protocol
// can route through a relay without hand-rolling that framing itself. Used
// by Examples/MultiplayerSandbox/net_lockstep.cpp in relay mode; nothing
// about it is specific to that protocol.
//
// UdpRelay strips the header before forwarding, so nothing needs to be
// undone on receive — forwarded packets arrive through the caller's own
// socket as plain payload, exactly as if they came directly from the peer.
// This class therefore only has a Send(); receiving is just the caller's own
// ordinary recvfrom() on the socket it already owns.
//
// The caller owns the socket (bind/recvfrom/close are unchanged from
// whatever it already does for its own direct, non-relay transport) — this
// class only resolves the relay's address once and prepends the room header
// on Send().
//
// Not available on Emscripten (no raw UDP sockets in the browser).
#ifndef __EMSCRIPTEN__

class UdpRelayClient {
public:
    // Resolves relayIp and remembers roomId; call once before Send().
    // Returns false if relayIp isn't a parseable IPv4 address.
    bool Connect(const std::string& relayIp, uint16_t relayPort, uint32_t roomId);

    // Sends `data` (len bytes, max 1500) to the relay via `sock`, prefixed
    // with the room header. `sock` is whatever UDP socket the caller already
    // has open for its own use; this never creates, binds, or closes a
    // socket itself.
    bool Send(UdpSocket& sock, const uint8_t* data, int len) const;

    bool IsConnected() const {
        return _connected;
    }
    uint32_t RoomId() const {
        return _roomId;
    }
    // Resolved relay address/port (network byte order). The caller should
    // treat this as its "peer" address for validating incoming datagrams —
    // UdpRelay forwards from itself, not from the original remote peer.
    uint32_t RelayAddr() const {
        return _relayAddr;
    }
    uint16_t RelayPort() const {
        return _relayPort;
    }

private:
    uint32_t _relayAddr = 0;// resolved IPv4 address, network byte order
    uint16_t _relayPort = 0;// network byte order
    uint32_t _roomId = 0;
    bool _connected = false;
};

#endif// !__EMSCRIPTEN__

#pragma once
#include "udp_socket.hpp"
#include <cstdint>
#include <string>
#include <unordered_map>

// UdpRelay
//
// Forwards UDP packets between two peers identified by a shared roomId.
// Intended for server builds where direct peer-to-peer NAT traversal has
// failed.
//
// Packet wire format (client → relay):
//   [roomId: uint32_t LE][...LockstepNet payload...]
//
// The relay strips the roomId header and forwards the bare payload to the
// other peer in the same room.  From LockstepNet's perspective the packets
// look exactly as if they arrived directly from the peer.
//
// Room lifecycle:
//   - A room is created automatically on the first packet received for a roomId.
//   - The first sender is registered as peer A; the second distinct sender as peer B.
//   - Once both peers are registered, packets flow bidirectionally.
//   - A peer that has been silent for kPeerStaleMs can be replaced by a new
//     sender address (handles mobile clients rebinding after a network switch).
//   - Rooms with no activity for kRoomTimeoutMs milliseconds are removed.
//
// Abuse mitigation: UDP has no handshake, so anyone can send a datagram
// claiming any roomId. maxRooms bounds total memory; maxNewRoomsPerIpPerWindow
// additionally throttles how fast any single source address can open new
// rooms, so one misbehaving client can't burn through the whole room budget
// and lock out real players. Neither defends against a distributed flood
// from many source addresses at once — that needs network-layer mitigation
// (firewalling the port to known ranges, a provider's DDoS protection, or a
// UDP-aware proxy like Cloudflare Spectrum in front of the relay).
//
// Usage (headless relay process — see Examples/RelayServer):
//   UdpRelay relay;
//   relay.Start(9000);
//   while (running) relay.Process(dt);
//
// A plain class, not an engine Subsystem: it has no per-frame Application
// dependency and no per-entity meaning, so it doesn't fit AddSubsystem<T>()
// or Component. If a project needs to embed it inside an Application-driven
// process (e.g. to show a debug panel), wrap it the way NetworkSubsystem
// wraps HttpClient/WebSocketClient: a thin Subsystem that owns a UdpRelay
// member and forwards Process()/DrawImGui() to it.
//
// Not available on Emscripten (no raw UDP sockets in the browser).
#ifndef __EMSCRIPTEN__

class UdpRelay {
public:
    static constexpr uint32_t kRoomTimeoutMs = 60'000;// 1 minute idle → evict room
    static constexpr uint32_t kPeerStaleMs = 5'000;// silent this long → replaceable

    // Cap on simultaneous rooms; packets that would create a room beyond this
    // are dropped. Guards the room map against packet-flood memory growth.
    int maxRooms = 1024;

    // Per-source-IP limit on how many NEW rooms may be created per
    // rateLimitWindowMs. Packets to an already-open room (ordinary gameplay
    // traffic) are never throttled — only the act of creating a room is.
    int maxNewRoomsPerIpPerWindow = 5;
    uint32_t rateLimitWindowMs = 60'000;// 1 minute

    // port 0 binds an OS-assigned ephemeral port; BoundPort() reports it.
    bool Start(uint16_t port);
    void Stop();

    // Call once per frame/tick to pump the socket and evict stale rooms.
    void Process(float dt);

    bool IsRunning() const {
        return _running;
    }
    uint16_t BoundPort() const {
        return _socket.BoundPort();
    }
    int RoomCount() const {
        return static_cast<int>(_rooms.size());
    }

private:
    struct Peer {
        uint32_t addr = 0;
        uint16_t port = 0;
        bool valid = false;
        uint32_t lastSeenMs = 0;
    };
    struct Room {
        Peer peers[2];
        uint32_t lastActivityMs = 0;
    };
    struct IpBudget {
        uint32_t windowStartMs = 0;
        int roomsThisWindow = 0;
    };

    UdpSocket _socket;
    bool _running = false;
    uint32_t _totalMs = 0;// accumulated from Process(dt)

    std::unordered_map<uint32_t, Room> _rooms;// roomId → Room
    std::unordered_map<uint32_t, IpBudget> _ipBudgets;// source addr → rate-limit state

    void _pump();
    void _evictStaleRooms();
    void _evictStaleIpBudgets();
    bool _allowNewRoom(uint32_t senderAddr);
    void _forwardTo(const Peer& dst, const uint8_t* payload, int len);
};

#endif// !__EMSCRIPTEN__

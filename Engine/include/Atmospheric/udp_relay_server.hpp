#pragma once
#include "server.hpp"
#include <cstdint>
#include <string>
#include <unordered_map>

// UdpRelayServer
//
// Engine-level subsystem that forwards UDP packets between two peers identified
// by a shared roomId.  Intended for server builds where direct peer-to-peer
// NAT traversal has failed.
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
// Usage (server build):
//   auto relay = app->AddSubsystem<UdpRelayServer>();
//   relay->Start(9000);
//
// Not available on Emscripten (no raw UDP sockets in the browser).
#ifndef __EMSCRIPTEN__

class UdpRelayServer : public Server {
public:
    static constexpr uint32_t kRoomTimeoutMs = 60'000; // 1 minute idle → evict room
    static constexpr uint32_t kPeerStaleMs   = 5'000;  // silent this long → replaceable

    // Cap on simultaneous rooms; packets that would create a room beyond this
    // are dropped. Guards the room map against packet-flood memory growth.
    int maxRooms = 1024;

    void Init(Application* app) override;
    void Process(float dt) override;
    void DrawImGui(float dt) override;

    // port 0 binds an OS-assigned ephemeral port; BoundPort() reports it.
    bool Start(uint16_t port);
    void Stop();

    bool     IsRunning()  const { return _running; }
    uint16_t BoundPort()  const { return _boundPort; }
    int      RoomCount()  const { return static_cast<int>(_rooms.size()); }

private:
    struct Peer {
        uint32_t addr = 0;
        uint16_t port = 0;
        bool     valid = false;
        uint32_t lastSeenMs = 0;
    };
    struct Room {
        Peer     peers[2];
        uint32_t lastActivityMs = 0;
    };

#if defined(_WIN32)
    using SocketHandle = uintptr_t;
    static constexpr SocketHandle kInvalidSocket = SocketHandle(~uintptr_t(0));
#else
    using SocketHandle = int;
    static constexpr SocketHandle kInvalidSocket = SocketHandle(-1);
#endif

    SocketHandle _sock      = kInvalidSocket;
    uint16_t     _boundPort = 0;
    bool         _running   = false;
    uint32_t     _totalMs   = 0;           // accumulated from Process(dt)

    std::unordered_map<uint32_t, Room> _rooms; // roomId → Room

    void _pump();
    void _evictStaleRooms();
    void _forwardTo(const Peer& dst, const uint8_t* payload, int len);
};

#endif // !__EMSCRIPTEN__

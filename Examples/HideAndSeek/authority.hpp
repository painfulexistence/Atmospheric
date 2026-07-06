#pragma once
#include "protocol.hpp"
#include "sim_common.hpp"
#include <Atmospheric/udp_socket.hpp>

#include <chrono>
#include <cstdint>

// HideAndSeekAuthority — the authoritative simulation + networking core shared
// by both server deployment shapes:
//
//   - HideAndSeekServer (server_main.cpp): a standalone process with no
//     attached player, driven by a plain loop.
//   - HideAndSeekListenServer (listen_server_main.cpp): embedded inside one
//     player's own windowed client process, driven from Application::OnUpdate
//     alongside that player's own rendering/input.
//
// Neither deployment changes the wire protocol or a connecting client's
// experience at all — ClientNet talks to this the same way either way, which
// is the whole point: authority (who decides what's true) is orthogonal to
// deployment (whether that authority lives in a dedicated process or inside
// a player's own game).
//
// The listen server's own local player talks to this exactly like any other
// client — over loopback UDP via its own ClientNet connected to 127.0.0.1 —
// rather than a special zero-latency code path. That's a deliberate
// simplicity tradeoff: the host pays a tiny bit of loopback round-trip
// latency a "real" listen-server implementation would usually special-case
// away, in exchange for the host exercising the exact same prediction/
// reconciliation path as every other client (one less thing to get subtly
// wrong twice).
//
// Demonstrates the three properties deterministic lockstep (see
// MultiplayerSandbox) can't give an asymmetric, hidden-information game:
//   1. Hidden information is never sent, not just render-hidden: the Hider's
//      position is only included in the Seeker's snapshot when within vision
//      radius. There is nothing in the Seeker's memory to read out of even
//      with a memory-scanning cheat tool — the server never sent it.
//   2. The server, not the client, owns movement speed: ClientInput carries
//      only a direction, and the server multiplies by its own fixed speed
//      and tick delta. A modified client reporting an inflated direction
//      gains nothing (sim::Step clamps direction to unit length first).
//   3. No cross-machine determinism is required at all — a single process
//      is the sole simulator, which is what makes properties 1 and 2
//      possible in the first place.
//
// Owns its own UDP socket; platform socket handling is entirely internal
// (see authority.cpp) so this header stays free of OS socket types.
class HideAndSeekAuthority {
public:
    ~HideAndSeekAuthority();

    // port 0 binds an OS-assigned ephemeral port; BoundPort() reports it.
    bool Bind(uint16_t port);
    void Shutdown();

    // Non-blocking: drains incoming packets, then — once per fixed tick,
    // paced by an internal clock rather than the caller's frame rate —
    // advances the simulation and sends a filtered snapshot to each
    // connected client. Call once per frame/loop iteration.
    void Pump();

    bool IsBound() const {
        return _socket.IsOpen();
    }
    uint16_t BoundPort() const {
        return _socket.BoundPort();
    }

private:
    struct ClientSlot {
        bool connected = false;
        uint32_t addr = 0;
        uint16_t port = 0;
        sim::Vec2 pos;
        float dx = 0.0f, dy = 0.0f;
        uint32_t lastInputTick = 0;
    };

    UdpSocket _socket;
    ClientSlot _slots[2];// indexed by proto::Role
    uint32_t _serverTick = 0;
    std::chrono::steady_clock::time_point _nextTick{};
    bool _tickScheduled = false;

    void ResetRound();
    void HandlePacket(const uint8_t* data, int len, uint32_t fromAddr, uint16_t fromPort);
    void SendTo(uint32_t addr, uint16_t port, const uint8_t* data, int len);
    void Tick();
};

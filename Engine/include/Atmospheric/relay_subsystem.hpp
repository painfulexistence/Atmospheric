#pragma once
#include "subsystem.hpp"

// Not available on Emscripten (UdpRelay itself needs raw UDP sockets).
#ifndef __EMSCRIPTEN__

#include "udp_relay.hpp"

// RelaySubsystem
//
// Thin Application-embeddable wrapper around UdpRelay, for projects that want
// the relay driven inside an Application-owned process — e.g. a server build
// that also runs matchmaking/game logic through the same Application, or a
// windowed dev tool with a live debug panel — instead of a bare hand-rolled
// loop (see Examples/RelayServer/main.cpp for that simpler alternative).
//
// Mirrors how NetworkSubsystem wraps HttpClient/WebSocketClient: the
// capability itself (UdpRelay) stays a plain class with no framework
// dependency, and this Subsystem only owns an instance and forwards the
// per-frame calls.
//
// Usage:
//   auto relay = app->AddSubsystem<RelaySubsystem>();
//   relay->Relay().Start(9000);
//
// Process(dt) is called automatically each frame by the Application; it
// pumps the relay's socket and evicts stale rooms / rate-limit budgets.
class RelaySubsystem : public Subsystem {
public:
    void Init(Application* app) override;
    void Process(float dt) override;
    void DrawImGui(float dt) override;

    UdpRelay& Relay() {
        return _relay;
    }

private:
    UdpRelay _relay;
};

#endif// !__EMSCRIPTEN__

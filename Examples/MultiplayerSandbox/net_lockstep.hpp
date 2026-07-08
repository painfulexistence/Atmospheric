#pragma once
#include "game_sim.hpp"
#include <Atmospheric/net_conditioner.hpp>
#include <Atmospheric/net_metrics.hpp>
#include <Atmospheric/udp_relay_client.hpp>
#include <Atmospheric/udp_socket.hpp>
#include <cstdint>
#include <string>
#include <unordered_map>

// Deterministic-lockstep UDP session for exactly two peers.
//
// Both peers run the same simulation; only InputFrames are exchanged.
// Each input packet redundantly carries the last N unacked frames, and is
// resent every pump, so isolated packet loss never stalls the simulation.
// Latency model: local input is applied after `inputDelay` ticks (default 3
// ticks = 50 ms at 60 Hz), which is the only added latency as long as the
// network round-trip stays below inputDelay * 16.7 ms.

class LockstepNet {
public:
    enum class Mode { Solo, Host, Client };
    enum class State { Idle, Connecting, Running, Failed };

    Mode mode = Mode::Solo;
    State state = State::Idle;
    uint32_t seed = 1;
    int inputDelay = 3;
    int localPlayer = 0;// host = player 0, client = player 1
    int rttMs = -1;
    bool desync = false;
    std::string error;

    // Relay mode: set by StartRelayHost/StartRelayClient. SendRaw delegates
    // to _relayClient (below) when useRelay is true.
    bool useRelay = false;

    void StartSolo(uint32_t seed);
    bool StartHost(uint16_t port, uint32_t seed, int delay);
    bool StartClient(const std::string& ip, uint16_t port);
    // Relay variants: both peers point at the relay server; roomId identifies the session.
    bool StartRelayHost(const std::string& relayIp, uint16_t relayPort, uint32_t roomId, uint32_t seed, int delay);
    bool StartRelayClient(const std::string& relayIp, uint16_t relayPort, uint32_t roomId);
    void Shutdown();

    // Call once per frame: receives packets, drives the handshake,
    // resends unacked inputs and keeps the RTT estimate fresh.
    void Pump(uint32_t nowMs);

    // Records (and immediately sends) the local input for `tick`.
    // Idempotent: repeated calls for the same tick are ignored.
    void SubmitLocalInput(uint32_t tick, const InputFrame& f);

    bool HasInputs(uint32_t tick) const;
    InputFrame GetInput(int player, uint32_t tick) const;

    // Exchange a state checksum for cross-peer desync detection.
    void ShareChecksum(uint32_t tick, uint32_t sum);

    // Drop stored inputs/checksums older than `tick` (call as the sim advances).
    void PruneBelow(uint32_t tick);

    // Netgraph data + the live inbound link emulator (mutable so HUD keybinds
    // can dial it). Unlike the server-auth examples, lockstep reacts to loss by
    // stalling (waiting for the missing input), not by mispredicting — the
    // netgraph makes that contrast visible.
    const NetMetrics& Metrics() const {
        return _metrics;
    }
    NetConditioner& Conditioner() {
        return _cond;
    }

private:
    NetConditioner _cond;
    NetMetrics _metrics;
    void UpdateMetrics(uint32_t nowMs);// pending/loss/bandwidth roll, shared by both Pump paths
    // Native only: UdpSocket/UdpRelayClient don't exist on Emscripten (no raw
    // UDP sockets in the browser — see their header comments). The web build
    // uses RTCDataChannel instead (see net_lockstep.cpp's __EMSCRIPTEN__
    // branch), so these members would sit unused there even if they could be
    // declared.
#ifndef __EMSCRIPTEN__
    UdpSocket _socket;
    UdpRelayClient _relayClient;// only touched when useRelay is true
#endif
    bool havePeer = false;
    uint32_t peerAddr = 0;
    uint16_t peerPort = 0;

    std::unordered_map<uint32_t, InputFrame> localInputs;
    std::unordered_map<uint32_t, InputFrame> remoteInputs;
    std::unordered_map<uint32_t, uint32_t> localChecks;
    uint32_t localTop = 0;// highest local tick submitted
    bool haveLocal = false;
    uint32_t peerAckedTick = 0;// highest local tick the peer confirmed
    uint32_t remoteTop = 0;// highest remote tick received
    uint32_t lastPingMs = 0;
    uint32_t lastHelloMs = 0;

    bool OpenSocket(uint16_t bindPort);
    void SendRaw(const uint8_t* data, int len);
    void SendHello(uint32_t nowMs);
    void SendWelcome();
    void SendInputs();
    void HandlePacket(const uint8_t* data, int len, uint32_t fromAddr, uint16_t fromPort, uint32_t nowMs);
};

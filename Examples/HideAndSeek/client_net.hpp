#pragma once
#include "protocol.hpp"
#include "sim_common.hpp"
#include <Atmospheric/net_conditioner.hpp>
#include <Atmospheric/net_metrics.hpp>
#include <Atmospheric/udp_socket.hpp>

#include <cstdint>
#include <map>
#include <string>

// ClientNet — client side of the HideAndSeek protocol.
//
// Prediction + reconciliation, simplified: rather than replaying a precise
// per-tick input history against the server's authoritative position (the
// textbook rewind-replay approach), this blends a fraction of the
// server/predicted difference back in on every snapshot ("error smoothing"),
// hard-snapping only on a large divergence (reconnect, a burst of loss).
// That's a legitimate simplification for a game with no frame-perfect
// precision requirements, at the cost of a little more visible correction
// lag than exact replay would give — see the README for the tradeoff.
//
// The remote entity (when visible at all — see HasRemote()) is rendered via
// simple two-sample interpolation with a fixed render delay, since its
// snapshots arrive slower than the render rate and it has no local input to
// predict from.
class ClientNet {
public:
    ~ClientNet();

    bool Connect(const std::string& serverIp, uint16_t serverPort, proto::Role role);

    // Advances local prediction for this tick and sends the input to the
    // server. Call once per fixed tick with the sampled input direction.
    // nowMs timestamps the send so RTT can be measured when the input is acked.
    void SubmitInput(uint32_t nowMs, uint32_t tick, float dx, float dy);

    // Non-blocking receive + processes any snapshots that arrived.
    void Pump(uint32_t nowMs);

    bool IsWelcomed() const {
        return _welcomed;
    }
    proto::Role GetRole() const {
        return _role;
    }
    sim::Vec2 GetOwnPos() const {
        return _predictedPos;
    }
    bool HasRemote() const {
        return _haveRemoteB;
    }
    // Interpolated position of the other entity, or {} if HasRemote() is false.
    sim::Vec2 GetRemotePos(uint32_t nowMs) const;

    // Netgraph data + the live inbound link emulator (mutable so HUD keybinds
    // can dial it). Sits on this client's snapshot path — works in --local too.
    const NetMetrics& Metrics() const {
        return _metrics;
    }
    NetConditioner& Conditioner() {
        return _cond;
    }

private:
    UdpSocket _socket;
    uint32_t _serverAddr = 0;
    uint16_t _serverPort = 0;
    proto::Role _role = proto::Role::Seeker;
    bool _welcomed = false;

    NetConditioner _cond;
    NetMetrics _metrics;
    std::map<uint32_t, uint32_t> _inputSendMs;// tick -> send time, for RTT on ack

    sim::Vec2 _predictedPos;

    static constexpr uint32_t kInterpDelayMs = 100;
    struct RemoteSample {
        sim::Vec2 pos;
        uint32_t recvMs = 0;
    };
    RemoteSample _remoteA, _remoteB;// A = older, B = newer
    bool _haveRemoteA = false, _haveRemoteB = false;

    void HandlePacket(const uint8_t* data, int len, uint32_t fromAddr, uint16_t fromPort, uint32_t nowMs);
    void HandleSnapshot(const uint8_t* data, int len, uint32_t nowMs);
};

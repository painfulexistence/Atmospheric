#pragma once
#include "protocol.hpp"
#include "sim_common.hpp"
#include <Atmospheric/net_conditioner.hpp>
#include <Atmospheric/net_metrics.hpp>
#include <Atmospheric/udp_socket.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

// ClientNet — client side of the 3D Deathmatch protocol.
//
// Prediction has three faces, same as the 2D lineage but now in 3D:
//   1. Own movement — predicted with yaw-relative sim::StepPlayer and
//      reconciled by exact rewind-replay: on each snapshot, reset to the
//      server's authoritative foot as of ackedInputTick and re-apply every
//      pending input. Zero residual on a clean link.
//   2. The enemy — not predicted; interpolated between the last two snapshots
//      in BOTH position and orientation (yaw via shortest-arc, pitch linear),
//      with a fixed render delay. That delay also defines renderTick, the tick
//      the server rewinds to for lag compensation.
//   3. Own rockets — cosmetic prediction, shown leaving the muzzle immediately.
//
// The client owns its own view (yaw/pitch from the mouse); the server never
// sends it back. It IS sent up every tick because movement is yaw-relative and
// each fire latches the view it was aimed with.
class ClientNet {
public:
    struct AuthRocket {
        uint16_t id = 0;
        int owner = 0;
        sim::Vec3 pos;
    };
    struct CosmeticRocket {
        sim::Vec3 pos;
        sim::Vec3 vel;
        float life = 0.0f;
    };

    bool Connect(const std::string& serverIp, uint16_t serverPort);

    // Predicts one tick of movement (incl. jump/levitate/dash), latches any
    // fire this tick (aimed along the current view), and sends the input.
    // nowMs timestamps the send so RTT can be measured when the input is acked.
    void SubmitInput(
        uint32_t nowMs,
        uint32_t tick,
        float forward,
        float strafe,
        float yaw,
        float pitch,
        bool jump,
        bool dash,
        bool shield,
        bool fireRail,
        bool fireRocket
    );

    void Pump(uint32_t nowMs);
    void UpdateCosmetic(float dt);

    bool IsWelcomed() const {
        return _welcomed;
    }
    int PlayerId() const {
        return _playerId;
    }
    sim::Vec3 GetOwnFoot() const {
        return _predictedMotion.foot;
    }
    int GetHealth() const {
        return _health;
    }
    bool IsAlive() const {
        return _alive;
    }
    bool IsShielded() const {
        return _shield;
    }
    bool EnemyShielded() const {
        return _enemyShield;
    }
    // 0 = dash ready, 1 = just used (for a HUD cooldown indicator).
    float DashCooldownFrac() const {
        return static_cast<float>(_predictedMotion.dashCd) / static_cast<float>(sim::kDashCooldownTicks);
    }
    int GetScore() const {
        return _score;
    }
    int GetEnemyScore() const {
        return _enemyScore;
    }
    bool HasEnemy() const {
        return _enemyAlive && _haveEnemyB;
    }
    sim::Vec3 GetEnemyFoot(uint32_t nowMs) const;
    float GetEnemyYaw(uint32_t nowMs) const;
    float GetEnemyPitch(uint32_t nowMs) const;

    const std::vector<AuthRocket>& AuthoritativeRockets() const {
        return _authRockets;
    }
    const std::vector<CosmeticRocket>& CosmeticRockets() const {
        return _cosRockets;
    }

    // Netgraph data + the live link emulator (mutable so the HUD keybinds can
    // dial latency/jitter/loss). The conditioner sits on this client's inbound
    // snapshot path, so it works even in --local loopback play.
    const NetMetrics& Metrics() const {
        return _metrics;
    }
    NetConditioner& Conditioner() {
        return _cond;
    }

private:
    struct Move {
        float forward = 0.0f, strafe = 0.0f, yaw = 0.0f;
        bool jump = false, dash = false;
    };

    static constexpr uint32_t kInterpDelayMs = 100;
    static constexpr uint32_t kInterpDelayTicks = 6;// ~100 ms at 60 Hz

    UdpSocket _socket;
    uint32_t _serverAddr = 0;
    uint16_t _serverPort = 0;

    NetConditioner _cond;
    NetMetrics _metrics;
    std::map<uint32_t, uint32_t> _inputSendMs;// tick -> send time, for RTT on ack

    bool _welcomed = false;
    int _playerId = 0;
    uint32_t _lastServerTick = 0;

    sim::Motion _predictedMotion;
    float _viewYaw = 0.0f, _viewPitch = 0.0f;
    int _health = sim::kMaxHealth;
    bool _alive = true;
    bool _shield = false;
    bool _enemyShield = false;
    int _score = 0;
    int _enemyScore = 0;

    std::map<uint32_t, Move> _pending;

    uint16_t _railSeq = 0;
    uint16_t _rocketSeq = 0;
    float _railYaw = 0.0f, _railPitch = 0.0f;
    float _rocketYaw = 0.0f, _rocketPitch = 0.0f;

    struct Sample {
        sim::Vec3 foot;
        float yaw = 0.0f, pitch = 0.0f;
        uint32_t recvMs = 0;
    };
    Sample _enemyA, _enemyB;
    bool _haveEnemyA = false, _haveEnemyB = false;
    bool _enemyAlive = false;

    std::vector<AuthRocket> _authRockets;
    std::vector<CosmeticRocket> _cosRockets;

    float InterpT(uint32_t nowMs) const;
    void HandlePacket(const uint8_t* data, int len, uint32_t fromAddr, uint16_t fromPort, uint32_t nowMs);
    void HandleSnapshot(const uint8_t* data, int len, uint32_t nowMs);
};

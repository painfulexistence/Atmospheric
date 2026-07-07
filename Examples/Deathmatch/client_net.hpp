#pragma once
#include "protocol.hpp"
#include "sim_common.hpp"
#include <Atmospheric/udp_socket.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

// ClientNet — client side of the Deathmatch protocol. Where HideAndSeek's
// ClientNet reconciled with error-smoothing (blend a fraction toward the
// server each snapshot), this one does the textbook exact rewind-replay:
//
//   - keeps every un-acked input in _pending;
//   - on each snapshot, resets the predicted position to the server's
//     authoritative value as of ackedInputTick, then re-applies every pending
//     input after that tick via the same sim::StepPlayer the server ran.
//
// Because the server consumes each input exactly once in order (see
// authority.hpp), on a clean link the replay reproduces the prediction with
// zero residual — the correction is invisible. This is the reconciliation a
// competitive game wants and the "path not taken" HideAndSeek's README points
// at.
//
// Three faces of prediction live here, deliberately:
//   1. own movement   — predicted + exact rewind-replay reconciliation (above)
//   2. the enemy      — NOT predicted; interpolated between the last two
//                       snapshots with a fixed render delay (no local input to
//                       predict it from). This render delay is also what
//                       defines renderTick, the value the server rewinds to
//                       for lag compensation.
//   3. own rockets    — cosmetic prediction: a local rocket is shown leaving
//                       the barrel immediately so firing feels responsive,
//                       while the server's authoritative rocket (replicated in
//                       snapshots) is the one that actually deals damage.
class ClientNet {
public:
    struct AuthRocket {
        uint16_t id = 0;
        int owner = 0;
        sim::Vec2 pos;
    };
    struct CosmeticRocket {
        sim::Vec2 pos;
        sim::Vec2 vel;
        float life = 0.0f;
    };

    bool Connect(const std::string& serverIp, uint16_t serverPort);

    // Predicts one tick of own movement, latches any fire this tick, and sends
    // the input. aim is the (unnormalized) aim direction used by whichever
    // weapon fired this tick.
    void SubmitInput(uint32_t tick, float mx, float my, bool fireRail, bool fireRocket, float aimx, float aimy);

    void Pump(uint32_t nowMs);// receive + process snapshots
    void UpdateCosmetic(float dt);// advance cosmetic own-rockets for rendering

    bool IsWelcomed() const {
        return _welcomed;
    }
    int PlayerId() const {
        return _playerId;
    }
    sim::Vec2 GetOwnPos() const {
        return _predictedPos;
    }
    int GetHealth() const {
        return _health;
    }
    bool IsAlive() const {
        return _alive;
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
    sim::Vec2 GetEnemyPos(uint32_t nowMs) const;

    const std::vector<AuthRocket>& AuthoritativeRockets() const {
        return _authRockets;
    }
    const std::vector<CosmeticRocket>& CosmeticRockets() const {
        return _cosRockets;
    }

private:
    struct Move {
        float mx = 0.0f, my = 0.0f;
    };

    static constexpr uint32_t kInterpDelayMs = 100;
    static constexpr uint32_t kInterpDelayTicks = 6;// ~100 ms at 60 Hz

    UdpSocket _socket;
    uint32_t _serverAddr = 0;
    uint16_t _serverPort = 0;

    bool _welcomed = false;
    int _playerId = 0;
    uint32_t _lastServerTick = 0;

    sim::Vec2 _predictedPos;
    int _health = sim::kMaxHealth;
    bool _alive = true;
    int _score = 0;
    int _enemyScore = 0;

    std::map<uint32_t, Move> _pending;// un-acked inputs, for exact replay

    uint16_t _railSeq = 0;
    uint16_t _rocketSeq = 0;
    sim::Vec2 _railAim{ 1.0f, 0.0f };
    sim::Vec2 _rocketAim{ 1.0f, 0.0f };

    struct Sample {
        sim::Vec2 pos;
        uint32_t recvMs = 0;
    };
    Sample _enemyA, _enemyB;// A older, B newer
    bool _haveEnemyA = false, _haveEnemyB = false;
    bool _enemyAlive = false;

    std::vector<AuthRocket> _authRockets;
    std::vector<CosmeticRocket> _cosRockets;

    void HandleSnapshot(const uint8_t* data, int len, uint32_t nowMs);
};

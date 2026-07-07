#pragma once
#include "protocol.hpp"
#include "sim_common.hpp"
#include <Atmospheric/udp_socket.hpp>

#include <chrono>
#include <cstdint>
#include <map>
#include <vector>

// DeathmatchAuthority — the authoritative simulation + networking core for a
// 2-player arena shooter, built to demonstrate the two things HideAndSeek's
// proximity-tag mechanic never needed: server-side lag compensation, and the
// favor-the-shooter vs favor-the-target tradeoff that only a game with
// ranged weapons forces you to decide.
//
// Two weapons, deliberately treated differently, because instantaneous and
// travelling shots demand different lag-comp answers:
//
//   Railgun (hitscan, instantaneous). Point-in-time lag compensation: when a
//   client fires, it tags the shot with renderTick — the server tick it was
//   actually looking at (its latest snapshot tick minus interpolation delay).
//   The server rewinds the *target* to renderTick and tests the ray against
//   where the target was on the shooter's screen, not where it is now. This
//   is "favor the shooter": the shot lands if it looked like a hit to the
//   player who fired it. A single rewind to one instant.
//
//   Rocket (projectile, travels). A *rolling* rewind, and the place the
//   shooter-vs-target tradeoff actually bites:
//     - favorShooterRockets = true  → the rocket is judged against the target
//       as it was `lagTicks` in the past at *every* step of its flight, so a
//       victim who has ducked behind the wall on their own screen can still
//       be killed by the them-of-a-moment-ago out in the open. This is the
//       infamous "I died behind cover", and it is strictly worse than the
//       hitscan version because the rewind offset compounds with travel time.
//     - favorShooterRockets = false → the rocket is judged against the
//       target's current position, the same authoritative rocket everyone
//       sees replicated; you can dodge what you see, at the cost of the
//       shooter having to lead more than their screen suggests.
//   The toggle exists precisely so the difference is observable — flip it and
//   watch behind-cover kills appear or disappear.
//
// Client-side reconciliation for this game is the textbook exact rewind-replay
// (see client_net.hpp), upgrading HideAndSeek's error-smoothing: the server
// consumes each client input exactly once, in order, so the client can reset
// to the server's authoritative state and replay its own un-acked inputs to
// reproduce its prediction with zero residual (on a clean link).
class DeathmatchAuthority {
public:
    ~DeathmatchAuthority();

    // Flip to demonstrate the favor-the-shooter downside for rockets. Hitscan
    // is always favor-the-shooter (nobody ships favor-the-target hitscan — it
    // feels terrible), so only rockets expose the choice.
    bool favorShooterRockets = true;

    bool Bind(uint16_t port);// port 0 → ephemeral; BoundPort() reports it
    void Shutdown();
    void Pump();// drain packets, then advance fixed ticks on an internal clock

    bool IsBound() const {
        return _socket.IsOpen();
    }
    uint16_t BoundPort() const {
        return _socket.BoundPort();
    }
    uint32_t ServerTick() const {
        return _serverTick;
    }

private:
    static constexpr int kHistoryTicks = 256;// ~4.3 s of position history at 60 Hz

    struct Move {
        float mx = 0.0f, my = 0.0f;
    };

    struct PlayerSlot {
        bool connected = false;
        uint32_t addr = 0;
        uint16_t port = 0;

        sim::Vec2 pos;
        int health = sim::kMaxHealth;
        bool alive = true;
        float respawnTimer = 0.0f;
        int score = 0;

        // Movement input, consumed one-per-client-tick in order for exact
        // reconciliation (see class comment).
        std::map<uint32_t, Move> inputBuffer;
        uint32_t lastConsumedTick = 0;
        bool everConsumed = false;

        // Latest processed fire sequence per weapon; a fire is acted on when
        // its sequence advances, so repeats (redundant resends) are ignored.
        uint16_t lastRailSeq = 0;
        uint16_t lastRocketSeq = 0;

        // Ring buffer of authoritative positions, indexed by serverTick, for
        // lag-compensated (rewound) hit detection against this player.
        sim::Vec2 posHistory[kHistoryTicks];
        uint32_t historyTick[kHistoryTicks] = { 0 };
        bool historyValid[kHistoryTicks] = { false };
    };

    struct Rocket {
        uint16_t id = 0;
        int ownerIdx = 0;
        sim::Vec2 pos;
        sim::Vec2 vel;
        bool favorShooter = true;
        uint32_t lagTicks = 0;// how far in the past the shooter's view was
        bool active = false;
    };

    UdpSocket _socket;
    PlayerSlot _slots[2];
    std::vector<Rocket> _rockets;
    uint16_t _nextRocketId = 1;

    uint32_t _serverTick = 0;
    std::chrono::steady_clock::time_point _nextTick{};
    bool _tickScheduled = false;

    void ResetPlayer(int idx);
    void HandlePacket(const uint8_t* data, int len, uint32_t fromAddr, uint16_t fromPort);
    int SlotForSender(uint32_t addr, uint16_t port) const;
    sim::Vec2 HistoricalPos(int idx, uint32_t tick) const;
    void ApplyDamage(int targetIdx, int dmg, int killerIdx);
    void FireRail(int shooterIdx, sim::Vec2 aim, uint32_t renderTick);
    void SpawnRocket(int shooterIdx, sim::Vec2 aim, uint32_t renderTick);
    void Tick();
    void SendSnapshot(int idx);
    void SendTo(uint32_t addr, uint16_t port, const uint8_t* data, int len);
};

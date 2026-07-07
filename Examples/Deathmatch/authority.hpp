#pragma once
#include "protocol.hpp"
#include "sim_common.hpp"
#include <Atmospheric/udp_socket.hpp>

#include <chrono>
#include <cstdint>
#include <map>
#include <vector>

// DeathmatchAuthority — authoritative simulation + networking core for a
// 2-player 3D arena shooter, demonstrating server-side lag compensation and
// the favor-the-shooter vs favor-the-target tradeoff.
//
// Two weapons, treated differently because instantaneous and travelling shots
// want different lag-comp answers:
//
//   Railgun (hitscan): point-in-time rewind. The client tags the shot with
//   renderTick (the server tick it was looking at). The server rewinds the
//   target capsule to renderTick and tests the ray against where the target
//   was on the shooter's screen — favor the shooter.
//
//   Rocket (projectile): a rolling rewind with a favorShooterRockets toggle.
//   favor-shooter judges the rocket against the target as it was lagTicks in
//   the past at every step of flight ("I died behind cover"); favor-target
//   judges it against the present, letting the victim dodge what they see.
//
// The target capsule is yaw-invariant, so only *position* history is kept for
// rewind — orientation doesn't affect the hitbox. The shooter's view angle
// arrives in the fire packet, so no shooter-orientation history is needed
// either.
//
// Movement is yaw-relative and the server consumes each client input exactly
// once in order, so the client's exact rewind-replay reconciliation (see
// client_net.hpp) reproduces its prediction with zero residual on a clean link.
class DeathmatchAuthority {
public:
    ~DeathmatchAuthority();

    bool favorShooterRockets = true;

    bool Bind(uint16_t port);
    void Shutdown();
    void Pump();

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
    static constexpr int kHistoryTicks = 256;// ~4.3 s at 60 Hz

    struct Move {
        float forward = 0.0f, strafe = 0.0f, yaw = 0.0f;
    };

    struct PlayerSlot {
        bool connected = false;
        uint32_t addr = 0;
        uint16_t port = 0;

        sim::Vec3 foot;// authoritative foot position (y = 0)
        float viewYaw = 0.0f, viewPitch = 0.0f;// latest reported view (for snapshot replication)
        int health = sim::kMaxHealth;
        bool alive = true;
        float respawnTimer = 0.0f;
        int score = 0;

        std::map<uint32_t, Move> inputBuffer;
        uint32_t lastConsumedTick = 0;
        bool everConsumed = false;

        uint16_t lastRailSeq = 0;
        uint16_t lastRocketSeq = 0;

        sim::Vec3 posHistory[kHistoryTicks];
        uint32_t historyTick[kHistoryTicks] = { 0 };
        bool historyValid[kHistoryTicks] = { false };
    };

    struct Rocket {
        uint16_t id = 0;
        int ownerIdx = 0;
        sim::Vec3 pos;
        sim::Vec3 vel;
        bool favorShooter = true;
        uint32_t lagTicks = 0;
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
    sim::Vec3 HistoricalPos(int idx, uint32_t tick) const;
    void ApplyDamage(int targetIdx, int dmg, int killerIdx);
    void FireRail(int shooterIdx, float yaw, float pitch, uint32_t renderTick);
    void SpawnRocket(int shooterIdx, float yaw, float pitch, uint32_t renderTick);
    void Tick();
    void SendSnapshot(int idx);
    void SendTo(uint32_t addr, uint16_t port, const uint8_t* data, int len);
};

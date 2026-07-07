#include "authority.hpp"

#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>

namespace {
    // Opposite sides of the arena, facing each other across the central pillar.
    const sim::Vec3 kSpawn[2] = { { -9.0f, 0.0f, 0.0f }, { 9.0f, 0.0f, 0.0f } };
    const float kSpawnYaw[2] = { sim::kPi * 0.5f, -sim::kPi * 0.5f };
}// namespace

DeathmatchAuthority::~DeathmatchAuthority() {
    Shutdown();
}

bool DeathmatchAuthority::Bind(uint16_t port) {
    if (_socket.IsOpen()) Shutdown();
    if (!_socket.Open(port)) {
        spdlog::error("DeathmatchAuthority: bind() failed on port {} (port in use?)", port);
        return false;
    }
    _serverTick = 0;
    _tickScheduled = false;
    _rockets.clear();
    for (int i = 0; i < 2; i++)
        ResetPlayer(i);
    spdlog::info(
        "DeathmatchAuthority: bound on UDP :{} (favorShooterRockets={})", _socket.BoundPort(), favorShooterRockets
    );
    return true;
}

void DeathmatchAuthority::Shutdown() {
    _socket.Close();
    _slots[0] = PlayerSlot{};
    _slots[1] = PlayerSlot{};
    _rockets.clear();
}

void DeathmatchAuthority::ResetPlayer(int idx) {
    PlayerSlot& p = _slots[idx];
    p.foot = kSpawn[idx];
    p.viewYaw = kSpawnYaw[idx];
    p.viewPitch = 0.0f;
    p.health = sim::kMaxHealth;
    p.alive = true;
    p.respawnTimer = 0.0f;
}

int DeathmatchAuthority::SlotForSender(uint32_t addr, uint16_t port) const {
    for (int i = 0; i < 2; i++) {
        if (_slots[i].connected && _slots[i].addr == addr && _slots[i].port == port) return i;
    }
    return -1;
}

sim::Vec3 DeathmatchAuthority::HistoricalPos(int idx, uint32_t tick) const {
    const PlayerSlot& p = _slots[idx];
    const int slot = static_cast<int>(tick % kHistoryTicks);
    if (p.historyValid[slot] && p.historyTick[slot] == tick) return p.posHistory[slot];
    return p.foot;// not in history → best-effort current
}

void DeathmatchAuthority::ApplyDamage(int targetIdx, int dmg, int killerIdx) {
    PlayerSlot& t = _slots[targetIdx];
    if (!t.alive) return;
    t.health -= dmg;
    if (t.health <= 0) {
        t.health = 0;
        t.alive = false;
        t.respawnTimer = sim::kRespawnDelay;
        if (killerIdx >= 0 && killerIdx != targetIdx) _slots[killerIdx].score++;
        spdlog::info("DeathmatchAuthority: player {} downed by {}", targetIdx, killerIdx);
    }
}

void DeathmatchAuthority::FireRail(int shooterIdx, float yaw, float pitch, uint32_t renderTick) {
    const PlayerSlot& shooter = _slots[shooterIdx];
    if (!shooter.alive) return;
    const int targetIdx = 1 - shooterIdx;
    if (!_slots[targetIdx].connected || !_slots[targetIdx].alive) return;

    const sim::Vec3 eye = shooter.foot + sim::Vec3{ 0.0f, sim::kEyeHeight, 0.0f };
    const sim::Vec3 viewDir = sim::ForwardDir(yaw, pitch);
    // Favor the shooter: test against where the target was on the shooter's
    // screen (rewound to renderTick), not where it is now.
    const sim::Vec3 targetFoot = HistoricalPos(targetIdx, renderTick);
    if (sim::RailHits(eye, viewDir, targetFoot)) {
        ApplyDamage(targetIdx, sim::kRailDamage, shooterIdx);
    }
}

void DeathmatchAuthority::SpawnRocket(int shooterIdx, float yaw, float pitch, uint32_t renderTick) {
    const PlayerSlot& shooter = _slots[shooterIdx];
    if (!shooter.alive) return;

    const sim::Vec3 eye = shooter.foot + sim::Vec3{ 0.0f, sim::kEyeHeight, 0.0f };
    const sim::Vec3 viewDir = sim::ForwardDir(yaw, pitch);
    Rocket r;
    r.id = _nextRocketId++;
    r.ownerIdx = shooterIdx;
    r.pos = eye;
    r.vel = viewDir * sim::kRocketSpeed;
    r.favorShooter = favorShooterRockets;
    r.lagTicks = (_serverTick > renderTick) ? (_serverTick - renderTick) : 0;
    r.active = true;
    _rockets.push_back(r);
}

void DeathmatchAuthority::HandlePacket(const uint8_t* data, int len, uint32_t fromAddr, uint16_t fromPort) {
    if (len < 5 || proto::GetU32(data) != proto::kMagic) return;
    const auto type = static_cast<proto::PacketType>(data[4]);

    if (type == proto::PacketType::ClientHello && len >= proto::kClientHelloLen) {
        int idx = SlotForSender(fromAddr, fromPort);
        if (idx < 0) {
            for (int i = 0; i < 2; i++) {
                if (!_slots[i].connected) {
                    idx = i;
                    break;
                }
            }
        }
        if (idx < 0) return;// server full
        PlayerSlot& p = _slots[idx];
        p = PlayerSlot{};
        p.connected = true;
        p.addr = fromAddr;
        p.port = fromPort;
        ResetPlayer(idx);
        spdlog::info("DeathmatchAuthority: player {} connected", idx);

        uint8_t buf[proto::kServerWelcomeLen];
        proto::PutU32(buf, proto::kMagic);
        buf[4] = static_cast<uint8_t>(proto::PacketType::ServerWelcome);
        buf[5] = static_cast<uint8_t>(idx);
        proto::PutF32(buf + 6, sim::kTickRateHz);
        SendTo(fromAddr, fromPort, buf, sizeof(buf));
        return;
    }

    if (type == proto::PacketType::ClientInput && len >= proto::kClientInputLen) {
        const int idx = SlotForSender(fromAddr, fromPort);
        if (idx < 0) return;
        PlayerSlot& p = _slots[idx];

        const uint32_t clientTick = proto::GetU32(data + 5);
        const uint32_t renderTick = proto::GetU32(data + 9);
        const float forward = proto::DequantizeAxis(static_cast<int8_t>(data[13]));
        const float strafe = proto::DequantizeAxis(static_cast<int8_t>(data[14]));
        const float viewYaw = proto::DequantizeYaw(proto::GetU16(data + 15));
        const float viewPitch = proto::DequantizePitch(proto::GetU16(data + 17));
        const uint16_t railSeq = proto::GetU16(data + 19);
        const float railYaw = proto::DequantizeYaw(proto::GetU16(data + 21));
        const float railPitch = proto::DequantizePitch(proto::GetU16(data + 23));
        const uint16_t rocketSeq = proto::GetU16(data + 25);
        const float rocketYaw = proto::DequantizeYaw(proto::GetU16(data + 27));
        const float rocketPitch = proto::DequantizePitch(proto::GetU16(data + 29));

        p.viewYaw = viewYaw;
        p.viewPitch = viewPitch;

        // Movement: buffer for in-order, once-only consumption (exact
        // reconciliation). yaw travels with the input because movement is
        // yaw-relative — the server must reproduce the exact yaw the client
        // predicted with.
        if (!p.everConsumed || clientTick > p.lastConsumedTick) {
            p.inputBuffer[clientTick] = Move{ forward, strafe, viewYaw };
        }

        // Fires: act on a sequence only when it advances; the latched yaw/pitch
        // ride along so a lost first packet doesn't drift the shot direction.
        if (railSeq != 0 && railSeq != p.lastRailSeq) {
            p.lastRailSeq = railSeq;
            FireRail(idx, railYaw, railPitch, renderTick);
        }
        if (rocketSeq != 0 && rocketSeq != p.lastRocketSeq) {
            p.lastRocketSeq = rocketSeq;
            SpawnRocket(idx, rocketYaw, rocketPitch, renderTick);
        }
    }
}

void DeathmatchAuthority::Tick() {
    const float dt = sim::kTickDt;

    // 1. Respawns.
    for (int i = 0; i < 2; i++) {
        PlayerSlot& p = _slots[i];
        if (p.connected && !p.alive) {
            p.respawnTimer -= dt;
            if (p.respawnTimer <= 0.0f) ResetPlayer(i);
        }
    }

    // 2. Consume each buffered movement input exactly once, in order.
    for (int i = 0; i < 2; i++) {
        PlayerSlot& p = _slots[i];
        if (!p.connected) continue;
        for (auto it = p.inputBuffer.begin(); it != p.inputBuffer.end();) {
            if (p.everConsumed && it->first <= p.lastConsumedTick) {
                it = p.inputBuffer.erase(it);
                continue;
            }
            if (p.alive) p.foot = sim::StepPlayer(p.foot, it->second.forward, it->second.strafe, it->second.yaw);
            p.lastConsumedTick = it->first;
            p.everConsumed = true;
            it = p.inputBuffer.erase(it);
        }
    }

    // 3. Record authoritative foot positions for this tick (lag-comp history).
    for (int i = 0; i < 2; i++) {
        PlayerSlot& p = _slots[i];
        const int slot = static_cast<int>(_serverTick % kHistoryTicks);
        p.posHistory[slot] = p.foot;
        p.historyTick[slot] = _serverTick;
        p.historyValid[slot] = true;
    }

    // 4. Advance rockets and resolve collisions.
    for (Rocket& r : _rockets) {
        if (!r.active) continue;
        r.pos = sim::StepRocket(r.pos, r.vel);
        if (!sim::InBounds(r.pos) || r.pos.y <= sim::kFloorY || sim::PointHitsBox(r.pos, sim::kRocketRadius)) {
            r.active = false;
            continue;
        }
        const int targetIdx = 1 - r.ownerIdx;
        PlayerSlot& target = _slots[targetIdx];
        if (!target.connected || !target.alive) continue;

        sim::Vec3 tf = target.foot;
        if (r.favorShooter) {
            const uint32_t lookup = (_serverTick > r.lagTicks) ? (_serverTick - r.lagTicks) : 0;
            tf = HistoricalPos(targetIdx, lookup);
        }
        if (sim::PointHitsCapsule(r.pos, tf, sim::kRocketRadius)) {
            ApplyDamage(targetIdx, sim::kRocketDamage, r.ownerIdx);
            r.active = false;
        }
    }
    _rockets.erase(
        std::remove_if(_rockets.begin(), _rockets.end(), [](const Rocket& r) { return !r.active; }), _rockets.end()
    );

    // 5. One snapshot per connected client.
    for (int i = 0; i < 2; i++) {
        if (_slots[i].connected) SendSnapshot(i);
    }
}

void DeathmatchAuthority::SendSnapshot(int idx) {
    const PlayerSlot& self = _slots[idx];
    const PlayerSlot& enemy = _slots[1 - idx];

    uint8_t buf[512];
    proto::PutU32(buf, proto::kMagic);
    buf[4] = static_cast<uint8_t>(proto::PacketType::ServerSnapshot);
    proto::PutU32(buf + 5, _serverTick);
    proto::PutU32(buf + 9, self.lastConsumedTick);
    proto::PutVec3(buf + 13, self.foot);
    buf[25] = static_cast<uint8_t>(self.health);
    buf[26] = self.alive ? 1 : 0;
    buf[27] = static_cast<uint8_t>(self.score);
    buf[28] = static_cast<uint8_t>(enemy.score);
    buf[29] = (enemy.connected && enemy.alive) ? 1 : 0;
    proto::PutVec3(buf + 30, enemy.foot);
    proto::PutU16(buf + 42, proto::QuantizeYaw(enemy.viewYaw));
    proto::PutU16(buf + 44, proto::QuantizePitch(enemy.viewPitch));
    buf[46] = static_cast<uint8_t>(enemy.health);

    int off = proto::kServerSnapshotHeaderLen;// 48
    int count = 0;
    for (const Rocket& r : _rockets) {
        if (!r.active || count >= proto::kMaxRocketsInSnapshot) continue;
        proto::PutU16(buf + off, r.id);
        buf[off + 2] = static_cast<uint8_t>(r.ownerIdx);
        proto::PutVec3(buf + off + 3, r.pos);
        off += proto::kRocketEntryLen;
        count++;
    }
    buf[47] = static_cast<uint8_t>(count);
    SendTo(self.addr, self.port, buf, off);
}

void DeathmatchAuthority::SendTo(uint32_t addr, uint16_t port, const uint8_t* data, int len) {
    _socket.SendTo(addr, port, data, len);
}

void DeathmatchAuthority::Pump() {
    if (!_socket.IsOpen()) return;

    uint8_t buf[128];
    for (;;) {
        uint32_t fromAddr = 0;
        uint16_t fromPort = 0;
        int n = _socket.RecvFrom(buf, static_cast<int>(sizeof(buf)), fromAddr, fromPort);
        if (n <= 0) break;
        HandlePacket(buf, n, fromAddr, fromPort);
    }

    const auto now = std::chrono::steady_clock::now();
    if (!_tickScheduled) {
        _nextTick = now
                    + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                        std::chrono::duration<double>(sim::kTickDt)
                    );
        _tickScheduled = true;
    }
    if (now >= _nextTick) {
        Tick();
        _serverTick++;
        _nextTick += std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(sim::kTickDt)
        );
    }
}

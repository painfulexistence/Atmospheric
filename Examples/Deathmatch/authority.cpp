#include "authority.hpp"

#include <cmath>
#include <spdlog/spdlog.h>

namespace {
    const sim::Vec2 kSpawn[2] = { { 80.0f, 300.0f }, { 720.0f, 300.0f } };

    // Normalizes an aim vector; returns false if it's ~zero (no direction).
    bool NormalizeAim(sim::Vec2& a) {
        const float len = std::sqrt(a.x * a.x + a.y * a.y);
        if (len < 1e-4f) return false;
        a.x /= len;
        a.y /= len;
        return true;
    }
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
    // Preserve connection identity + score across a respawn; reset the rest.
    p.pos = kSpawn[idx];
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

sim::Vec2 DeathmatchAuthority::HistoricalPos(int idx, uint32_t tick) const {
    const PlayerSlot& p = _slots[idx];
    const int slot = static_cast<int>(tick % kHistoryTicks);
    if (p.historyValid[slot] && p.historyTick[slot] == tick) return p.posHistory[slot];
    return p.pos;// not in history (too old / not yet recorded) → best-effort current
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

void DeathmatchAuthority::FireRail(int shooterIdx, sim::Vec2 aim, uint32_t renderTick) {
    if (!NormalizeAim(aim)) return;
    const PlayerSlot& shooter = _slots[shooterIdx];
    if (!shooter.alive) return;
    const int targetIdx = 1 - shooterIdx;
    if (!_slots[targetIdx].connected || !_slots[targetIdx].alive) return;

    // Favor the shooter: test the ray against where the target was on the
    // shooter's screen (rewound to renderTick), not where it is now.
    const sim::Vec2 targetPos = HistoricalPos(targetIdx, renderTick);
    if (sim::RailHits(shooter.pos, aim, targetPos)) {
        ApplyDamage(targetIdx, sim::kRailDamage, shooterIdx);
    }
}

void DeathmatchAuthority::SpawnRocket(int shooterIdx, sim::Vec2 aim, uint32_t renderTick) {
    if (!NormalizeAim(aim)) return;
    const PlayerSlot& shooter = _slots[shooterIdx];
    if (!shooter.alive) return;

    Rocket r;
    r.id = _nextRocketId++;
    r.ownerIdx = shooterIdx;
    r.pos = shooter.pos;
    r.vel = { aim.x * sim::kRocketSpeed, aim.y * sim::kRocketSpeed };
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
        const float mx = proto::DequantizeAxis(static_cast<int8_t>(data[13]));
        const float my = proto::DequantizeAxis(static_cast<int8_t>(data[14]));
        const uint16_t railSeq = proto::GetU16(data + 15);
        const sim::Vec2 railAim = { proto::DequantizeAxis(static_cast<int8_t>(data[17])),
                                    proto::DequantizeAxis(static_cast<int8_t>(data[18])) };
        const uint16_t rocketSeq = proto::GetU16(data + 19);
        const sim::Vec2 rocketAim = { proto::DequantizeAxis(static_cast<int8_t>(data[21])),
                                      proto::DequantizeAxis(static_cast<int8_t>(data[22])) };

        // Movement: buffer for in-order, once-only consumption (exact
        // reconciliation). Ignore inputs we've already consumed.
        if (!p.everConsumed || clientTick > p.lastConsumedTick) {
            p.inputBuffer[clientTick] = Move{ mx, my };
        }

        // Fires: act on a sequence only when it advances; repeats (redundant
        // resends that make a fire survive packet loss) are dedup'd here.
        if (railSeq != 0 && railSeq != p.lastRailSeq) {
            p.lastRailSeq = railSeq;
            FireRail(idx, railAim, renderTick);
        }
        if (rocketSeq != 0 && rocketSeq != p.lastRocketSeq) {
            p.lastRocketSeq = rocketSeq;
            SpawnRocket(idx, rocketAim, renderTick);
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
            if (p.alive) p.pos = sim::StepPlayer(p.pos, it->second.mx, it->second.my);
            p.lastConsumedTick = it->first;
            p.everConsumed = true;
            it = p.inputBuffer.erase(it);
        }
    }

    // 3. Record authoritative positions for this tick (lag-comp history).
    for (int i = 0; i < 2; i++) {
        PlayerSlot& p = _slots[i];
        const int slot = static_cast<int>(_serverTick % kHistoryTicks);
        p.posHistory[slot] = p.pos;
        p.historyTick[slot] = _serverTick;
        p.historyValid[slot] = true;
    }

    // 4. Advance rockets and resolve collisions.
    for (Rocket& r : _rockets) {
        if (!r.active) continue;
        r.pos = sim::StepRocket(r.pos, r.vel);
        if (!sim::InBounds(r.pos) || sim::CircleHitsWall(r.pos, sim::kRocketRadius)) {
            r.active = false;
            continue;
        }
        const int targetIdx = 1 - r.ownerIdx;
        PlayerSlot& target = _slots[targetIdx];
        if (!target.connected || !target.alive) continue;

        // favor-shooter → judge against the target as it was lagTicks ago
        // (the rolling rewind); favor-target → judge against the here-and-now
        // the victim can actually see and dodge.
        sim::Vec2 tp = target.pos;
        if (r.favorShooter) {
            const uint32_t lookup = (_serverTick > r.lagTicks) ? (_serverTick - r.lagTicks) : 0;
            tp = HistoricalPos(targetIdx, lookup);
        }
        if (sim::CircleCircle(r.pos, sim::kRocketRadius, tp, sim::kPlayerRadius)) {
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

    uint8_t buf[256];
    proto::PutU32(buf, proto::kMagic);
    buf[4] = static_cast<uint8_t>(proto::PacketType::ServerSnapshot);
    proto::PutU32(buf + 5, _serverTick);
    proto::PutU32(buf + 9, self.lastConsumedTick);
    proto::PutF32(buf + 13, self.pos.x);
    proto::PutF32(buf + 17, self.pos.y);
    buf[21] = static_cast<uint8_t>(self.health);
    buf[22] = self.alive ? 1 : 0;
    buf[23] = static_cast<uint8_t>(self.score);
    buf[24] = static_cast<uint8_t>(enemy.score);
    buf[25] = (enemy.connected && enemy.alive) ? 1 : 0;
    proto::PutF32(buf + 26, enemy.pos.x);
    proto::PutF32(buf + 30, enemy.pos.y);
    buf[34] = static_cast<uint8_t>(enemy.health);

    int off = proto::kServerSnapshotHeaderLen;// 36
    int count = 0;
    for (const Rocket& r : _rockets) {
        if (!r.active || count >= proto::kMaxRocketsInSnapshot) continue;
        proto::PutU16(buf + off, r.id);
        buf[off + 2] = static_cast<uint8_t>(r.ownerIdx);
        proto::PutF32(buf + off + 3, r.pos.x);
        proto::PutF32(buf + off + 7, r.pos.y);
        off += proto::kRocketEntryLen;
        count++;
    }
    buf[35] = static_cast<uint8_t>(count);
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

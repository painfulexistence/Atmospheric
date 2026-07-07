#include "client_net.hpp"

#include <cmath>
#include <spdlog/spdlog.h>

namespace {
    float Lerp(float a, float b, float t) {
        return a + (b - a) * t;
    }
    sim::Vec3 LerpVec3(sim::Vec3 a, sim::Vec3 b, float t) {
        return { Lerp(a.x, b.x, t), Lerp(a.y, b.y, t), Lerp(a.z, b.z, t) };
    }
}// namespace

bool ClientNet::Connect(const std::string& serverIp, uint16_t serverPort) {
    if (!_socket.Open(0)) return false;
    if (!UdpSocket::Resolve(serverIp, serverPort, _serverAddr, _serverPort)) {
        spdlog::error("ClientNet: invalid server address: {}", serverIp);
        return false;
    }
    _predictedFoot = { -9.0f, 0.0f, 0.0f };// provisional until first snapshot

    uint8_t buf[proto::kClientHelloLen];
    proto::PutU32(buf, proto::kMagic);
    buf[4] = static_cast<uint8_t>(proto::PacketType::ClientHello);
    _socket.SendTo(_serverAddr, _serverPort, buf, sizeof(buf));
    return true;
}

void ClientNet::SubmitInput(
    uint32_t tick, float forward, float strafe, float yaw, float pitch, bool fireRail, bool fireRocket
) {
    if (!_socket.IsOpen()) return;
    _viewYaw = yaw;
    _viewPitch = pitch;

    if (_alive) _predictedFoot = sim::StepPlayer(_predictedFoot, forward, strafe, yaw);
    _pending[tick] = Move{ forward, strafe, yaw };

    if (fireRail) {
        _railSeq = _railSeq == 0xFFFF ? 1 : _railSeq + 1;
        _railYaw = yaw;
        _railPitch = pitch;
    }
    if (fireRocket) {
        _rocketSeq = _rocketSeq == 0xFFFF ? 1 : _rocketSeq + 1;
        _rocketYaw = yaw;
        _rocketPitch = pitch;
        CosmeticRocket c;
        c.pos = _predictedFoot + sim::Vec3{ 0.0f, sim::kEyeHeight, 0.0f };
        c.vel = sim::ForwardDir(yaw, pitch) * sim::kRocketSpeed;
        c.life = 2.0f;
        _cosRockets.push_back(c);
    }

    const uint32_t renderTick = _lastServerTick > kInterpDelayTicks ? _lastServerTick - kInterpDelayTicks : 0;

    uint8_t buf[proto::kClientInputLen];
    proto::PutU32(buf, proto::kMagic);
    buf[4] = static_cast<uint8_t>(proto::PacketType::ClientInput);
    proto::PutU32(buf + 5, tick);
    proto::PutU32(buf + 9, renderTick);
    buf[13] = static_cast<uint8_t>(proto::QuantizeAxis(forward));
    buf[14] = static_cast<uint8_t>(proto::QuantizeAxis(strafe));
    proto::PutU16(buf + 15, proto::QuantizeYaw(yaw));
    proto::PutU16(buf + 17, proto::QuantizePitch(pitch));
    proto::PutU16(buf + 19, _railSeq);
    proto::PutU16(buf + 21, proto::QuantizeYaw(_railYaw));
    proto::PutU16(buf + 23, proto::QuantizePitch(_railPitch));
    proto::PutU16(buf + 25, _rocketSeq);
    proto::PutU16(buf + 27, proto::QuantizeYaw(_rocketYaw));
    proto::PutU16(buf + 29, proto::QuantizePitch(_rocketPitch));
    _socket.SendTo(_serverAddr, _serverPort, buf, sizeof(buf));
}

void ClientNet::Pump(uint32_t nowMs) {
    if (!_socket.IsOpen()) return;
    uint8_t buf[512];
    for (;;) {
        uint32_t fromAddr = 0;
        uint16_t fromPort = 0;
        int n = _socket.RecvFrom(buf, static_cast<int>(sizeof(buf)), fromAddr, fromPort);
        if (n <= 0) break;
        if (fromAddr != _serverAddr || fromPort != _serverPort) continue;
        if (n < 5 || proto::GetU32(buf) != proto::kMagic) continue;
        const auto type = static_cast<proto::PacketType>(buf[4]);
        if (type == proto::PacketType::ServerWelcome && n >= proto::kServerWelcomeLen) {
            _welcomed = true;
            _playerId = buf[5];
            spdlog::info("ClientNet: welcomed as player {}", _playerId);
        } else if (type == proto::PacketType::ServerSnapshot && n >= proto::kServerSnapshotHeaderLen) {
            HandleSnapshot(buf, n, nowMs);
        }
    }
}

void ClientNet::HandleSnapshot(const uint8_t* data, int len, uint32_t nowMs) {
    _lastServerTick = proto::GetU32(data + 5);
    const uint32_t ackedInputTick = proto::GetU32(data + 9);
    const sim::Vec3 serverFoot = proto::GetVec3(data + 13);
    _health = data[25];
    _alive = data[26] != 0;
    _score = data[27];
    _enemyScore = data[28];
    _enemyAlive = data[29] != 0;
    const sim::Vec3 enemyFoot = proto::GetVec3(data + 30);
    const float enemyYaw = proto::DequantizeYaw(proto::GetU16(data + 42));
    const float enemyPitch = proto::DequantizePitch(proto::GetU16(data + 44));

    // Exact rewind-replay reconciliation.
    _predictedFoot = serverFoot;
    for (auto it = _pending.begin(); it != _pending.end();) {
        if (it->first <= ackedInputTick) {
            it = _pending.erase(it);
        } else {
            if (_alive)
                _predictedFoot = sim::StepPlayer(_predictedFoot, it->second.forward, it->second.strafe, it->second.yaw);
            ++it;
        }
    }

    // Enemy: two-sample interpolation of position AND orientation.
    if (_enemyAlive) {
        _enemyA = _enemyB;
        _haveEnemyA = _haveEnemyB;
        _enemyB = { enemyFoot, enemyYaw, enemyPitch, nowMs };
        _haveEnemyB = true;
    } else {
        _haveEnemyA = _haveEnemyB = false;
    }

    // Authoritative rockets.
    _authRockets.clear();
    const uint8_t numRockets = data[47];
    int off = proto::kServerSnapshotHeaderLen;
    for (uint8_t i = 0; i < numRockets; i++) {
        if (off + proto::kRocketEntryLen > len) break;
        AuthRocket r;
        r.id = proto::GetU16(data + off);
        r.owner = data[off + 2];
        r.pos = proto::GetVec3(data + off + 3);
        _authRockets.push_back(r);
        off += proto::kRocketEntryLen;
    }
}

void ClientNet::UpdateCosmetic(float dt) {
    for (auto& c : _cosRockets) {
        c.pos = sim::StepRocket(c.pos, c.vel);
        c.life -= dt;
        if (c.pos.y <= sim::kFloorY || !sim::InBounds(c.pos) || sim::PointHitsBox(c.pos, sim::kRocketRadius))
            c.life = 0.0f;
    }
    _cosRockets.erase(
        std::remove_if(_cosRockets.begin(), _cosRockets.end(), [](const CosmeticRocket& c) { return c.life <= 0.0f; }),
        _cosRockets.end()
    );
}

float ClientNet::InterpT(uint32_t nowMs) const {
    if (!_haveEnemyA || _enemyB.recvMs <= _enemyA.recvMs) return 1.0f;
    const uint32_t renderTime = (nowMs > kInterpDelayMs) ? nowMs - kInterpDelayMs : 0;
    const float span = static_cast<float>(_enemyB.recvMs - _enemyA.recvMs);
    float t = static_cast<float>(renderTime) - static_cast<float>(_enemyA.recvMs);
    t = (t < 0.0f) ? 0.0f : (t > span ? span : t);
    return t / span;
}

sim::Vec3 ClientNet::GetEnemyFoot(uint32_t nowMs) const {
    if (!_haveEnemyB) return {};
    if (!_haveEnemyA) return _enemyB.foot;
    return LerpVec3(_enemyA.foot, _enemyB.foot, InterpT(nowMs));
}

float ClientNet::GetEnemyYaw(uint32_t nowMs) const {
    if (!_haveEnemyB) return 0.0f;
    if (!_haveEnemyA) return _enemyB.yaw;
    return sim::LerpYaw(_enemyA.yaw, _enemyB.yaw, InterpT(nowMs));
}

float ClientNet::GetEnemyPitch(uint32_t nowMs) const {
    if (!_haveEnemyB) return 0.0f;
    if (!_haveEnemyA) return _enemyB.pitch;
    return Lerp(_enemyA.pitch, _enemyB.pitch, InterpT(nowMs));
}

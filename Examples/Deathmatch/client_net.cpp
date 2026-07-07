#include "client_net.hpp"

#include <spdlog/spdlog.h>

namespace {
    float Lerp(float a, float b, float t) {
        return a + (b - a) * t;
    }
}// namespace

bool ClientNet::Connect(const std::string& serverIp, uint16_t serverPort) {
    if (!_socket.Open(0)) return false;
    if (!UdpSocket::Resolve(serverIp, serverPort, _serverAddr, _serverPort)) {
        spdlog::error("ClientNet: invalid server address: {}", serverIp);
        return false;
    }
    _predictedPos = { 80.0f, 300.0f };// provisional until first snapshot

    uint8_t buf[proto::kClientHelloLen];
    proto::PutU32(buf, proto::kMagic);
    buf[4] = static_cast<uint8_t>(proto::PacketType::ClientHello);
    _socket.SendTo(_serverAddr, _serverPort, buf, sizeof(buf));
    return true;
}

void ClientNet::SubmitInput(uint32_t tick, float mx, float my, bool fireRail, bool fireRocket, float aimx, float aimy) {
    if (!_socket.IsOpen()) return;

    // Predict own movement immediately and remember the input for replay.
    if (_alive) _predictedPos = sim::StepPlayer(_predictedPos, mx, my);
    _pending[tick] = Move{ mx, my };

    // Latch fires: bump the sequence and remember the aim; the packet carries
    // it (and keeps carrying it in later packets until the next fire), so a
    // dropped fire packet is covered by the next one.
    if (fireRail) {
        _railSeq = _railSeq == 0xFFFF ? 1 : _railSeq + 1;
        _railAim = { aimx, aimy };
    }
    if (fireRocket) {
        _rocketSeq = _rocketSeq == 0xFFFF ? 1 : _rocketSeq + 1;
        _rocketAim = { aimx, aimy };
        // Cosmetic prediction: show our own rocket leaving now, before the
        // server's authoritative one is replicated back to us.
        float len = std::sqrt(aimx * aimx + aimy * aimy);
        if (len > 1e-4f) {
            CosmeticRocket c;
            c.pos = _predictedPos;
            c.vel = { aimx / len * sim::kRocketSpeed, aimy / len * sim::kRocketSpeed };
            c.life = 2.0f;
            _cosRockets.push_back(c);
        }
    }

    // renderTick = the server tick we're actually looking at (latest received,
    // minus the interpolation delay the enemy is rendered at). This is the
    // value the server rewinds to for lag compensation.
    const uint32_t renderTick = _lastServerTick > kInterpDelayTicks ? _lastServerTick - kInterpDelayTicks : 0;

    uint8_t buf[proto::kClientInputLen];
    proto::PutU32(buf, proto::kMagic);
    buf[4] = static_cast<uint8_t>(proto::PacketType::ClientInput);
    proto::PutU32(buf + 5, tick);
    proto::PutU32(buf + 9, renderTick);
    buf[13] = static_cast<uint8_t>(proto::QuantizeAxis(mx));
    buf[14] = static_cast<uint8_t>(proto::QuantizeAxis(my));
    proto::PutU16(buf + 15, _railSeq);
    buf[17] = static_cast<uint8_t>(proto::QuantizeAxis(_railAim.x));
    buf[18] = static_cast<uint8_t>(proto::QuantizeAxis(_railAim.y));
    proto::PutU16(buf + 19, _rocketSeq);
    buf[21] = static_cast<uint8_t>(proto::QuantizeAxis(_rocketAim.x));
    buf[22] = static_cast<uint8_t>(proto::QuantizeAxis(_rocketAim.y));
    _socket.SendTo(_serverAddr, _serverPort, buf, sizeof(buf));
}

void ClientNet::Pump(uint32_t nowMs) {
    if (!_socket.IsOpen()) return;
    uint8_t buf[256];
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
    const sim::Vec2 serverPos{ proto::GetF32(data + 13), proto::GetF32(data + 17) };
    _health = data[21];
    _alive = data[22] != 0;
    _score = data[23];
    _enemyScore = data[24];
    _enemyAlive = data[25] != 0;
    const sim::Vec2 enemyPos{ proto::GetF32(data + 26), proto::GetF32(data + 30) };

    // Exact rewind-replay reconciliation: snap to the server's authoritative
    // state as of ackedInputTick, drop everything it has already accounted
    // for, then replay the inputs it hasn't seen yet.
    _predictedPos = serverPos;
    for (auto it = _pending.begin(); it != _pending.end();) {
        if (it->first <= ackedInputTick) {
            it = _pending.erase(it);
        } else {
            if (_alive) _predictedPos = sim::StepPlayer(_predictedPos, it->second.mx, it->second.my);
            ++it;
        }
    }

    // Enemy: two-sample interpolation (no local input to predict it from).
    if (_enemyAlive) {
        _enemyA = _enemyB;
        _haveEnemyA = _haveEnemyB;
        _enemyB = { enemyPos, nowMs };
        _haveEnemyB = true;
    } else {
        _haveEnemyA = _haveEnemyB = false;
    }

    // Authoritative rockets: replace the whole set each snapshot.
    _authRockets.clear();
    const uint8_t numRockets = data[35];
    int off = proto::kServerSnapshotHeaderLen;
    for (uint8_t i = 0; i < numRockets; i++) {
        if (off + proto::kRocketEntryLen > len) break;
        AuthRocket r;
        r.id = proto::GetU16(data + off);
        r.owner = data[off + 2];
        r.pos = { proto::GetF32(data + off + 3), proto::GetF32(data + off + 7) };
        _authRockets.push_back(r);
        off += proto::kRocketEntryLen;
    }
}

void ClientNet::UpdateCosmetic(float dt) {
    for (auto& c : _cosRockets) {
        c.pos = sim::StepRocket(c.pos, c.vel);
        c.life -= dt;
        if (sim::CircleHitsWall(c.pos, sim::kRocketRadius) || !sim::InBounds(c.pos)) c.life = 0.0f;
    }
    _cosRockets.erase(
        std::remove_if(_cosRockets.begin(), _cosRockets.end(), [](const CosmeticRocket& c) { return c.life <= 0.0f; }),
        _cosRockets.end()
    );
}

sim::Vec2 ClientNet::GetEnemyPos(uint32_t nowMs) const {
    if (!_haveEnemyB) return {};
    if (!_haveEnemyA || _enemyB.recvMs <= _enemyA.recvMs) return _enemyB.pos;

    const uint32_t renderTime = (nowMs > kInterpDelayMs) ? nowMs - kInterpDelayMs : 0;
    const float span = static_cast<float>(_enemyB.recvMs - _enemyA.recvMs);
    float t = static_cast<float>(renderTime) - static_cast<float>(_enemyA.recvMs);
    t = (t < 0.0f) ? 0.0f : (t > span ? span : t);
    t /= span;
    return { Lerp(_enemyA.pos.x, _enemyB.pos.x, t), Lerp(_enemyA.pos.y, _enemyB.pos.y, t) };
}

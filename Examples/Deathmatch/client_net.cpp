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
    if (!DatagramSocket::Resolve(serverIp, serverPort, _serverAddr, _serverPort)) {
        spdlog::error("ClientNet: invalid server address: {}", serverIp);
        return false;
    }
    _predictedMotion.foot = { -9.0f, 0.0f, 0.0f };// provisional until first snapshot
    SendHello();// retried from Pump() until the welcome arrives
    return true;
}

#ifdef __EMSCRIPTEN__
bool ClientNet::ConnectUrl(const std::string& url) {
    if (!_wt.Connect(url)) return false;
    _useWt = true;
    // A WebTransport session has no (addr,port); the shim reports (0,0), so
    // zero the expected sender and HandlePacket's filter accepts the session.
    _serverAddr = 0;
    _serverPort = 0;
    _predictedMotion.foot = { -9.0f, 0.0f, 0.0f };
    // No hello yet: the session is still handshaking. Pump()'s retry sends it
    // as soon as TransportReady() turns true.
    return true;
}
#endif

bool ClientNet::TransportReady() const {
#ifdef __EMSCRIPTEN__
    if (_useWt) return _wt.IsOpen();
#endif
    return _socket.IsOpen();
}

void ClientNet::SendToServer(const uint8_t* data, int len) {
#ifdef __EMSCRIPTEN__
    if (_useWt) {
        _wt.Send(data, len);
        _metrics.OnSent(len);
        return;
    }
#endif
    _socket.SendTo(_serverAddr, _serverPort, data, len);
    _metrics.OnSent(len);
}

void ClientNet::SendHello() {
    if (!TransportReady()) return;
    uint8_t buf[proto::kClientHelloLen];
    proto::PutU32(buf, proto::kMagic);
    buf[4] = static_cast<uint8_t>(proto::PacketType::ClientHello);
    SendToServer(buf, sizeof(buf));
}

void ClientNet::SubmitInput(
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
) {
    if (!TransportReady()) return;
    _viewYaw = yaw;
    _viewPitch = pitch;
    _shield = shield;

    if (_alive) _predictedMotion = sim::StepPlayer(_predictedMotion, forward, strafe, yaw, jump, dash);
    _pending[tick] = Move{ forward, strafe, yaw, jump, dash };

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
        c.pos = _predictedMotion.foot + sim::Vec3{ 0.0f, sim::kEyeHeight, 0.0f };
        c.vel = sim::ForwardDir(yaw, pitch) * sim::kRocketSpeed;
        c.life = 2.0f;
        _cosRockets.push_back(c);
    }

    uint8_t buttons = 0;
    if (jump) buttons |= proto::kBtnJump;
    if (dash) buttons |= proto::kBtnDash;
    if (shield) buttons |= proto::kBtnShield;

    const uint32_t renderTick = _lastServerTick > kInterpDelayTicks ? _lastServerTick - kInterpDelayTicks : 0;

    uint8_t buf[proto::kClientInputLen];
    proto::PutU32(buf, proto::kMagic);
    buf[4] = static_cast<uint8_t>(proto::PacketType::ClientInput);
    proto::PutU32(buf + 5, tick);
    proto::PutU32(buf + 9, renderTick);
    buf[13] = static_cast<uint8_t>(proto::QuantizeAxis(forward));
    buf[14] = static_cast<uint8_t>(proto::QuantizeAxis(strafe));
    buf[15] = buttons;
    proto::PutU16(buf + 16, proto::QuantizeYaw(yaw));
    proto::PutU16(buf + 18, proto::QuantizePitch(pitch));
    proto::PutU16(buf + 20, _railSeq);
    proto::PutU16(buf + 22, proto::QuantizeYaw(_railYaw));
    proto::PutU16(buf + 24, proto::QuantizePitch(_railPitch));
    proto::PutU16(buf + 26, _rocketSeq);
    proto::PutU16(buf + 28, proto::QuantizeYaw(_rocketYaw));
    proto::PutU16(buf + 30, proto::QuantizePitch(_rocketPitch));
    SendToServer(buf, sizeof(buf));
    _inputSendMs[tick] = nowMs;// matched against ackedInputTick for RTT
}

void ClientNet::HandlePacket(const uint8_t* buf, int n, uint32_t fromAddr, uint16_t fromPort, uint32_t nowMs) {
    if (fromAddr != _serverAddr || fromPort != _serverPort) return;
    if (n < 5 || proto::GetU32(buf) != proto::kMagic) return;
    _lastRecvMs = nowMs;// any valid server packet feeds the silence watchdog
    const auto type = static_cast<proto::PacketType>(buf[4]);
    if (type == proto::PacketType::ServerWelcome && n >= proto::kServerWelcomeLen) {
        _welcomed = true;
        _playerId = buf[5];
        spdlog::info("ClientNet: welcomed as player {}", _playerId);
    } else if (type == proto::PacketType::ServerSnapshot && n >= proto::kServerSnapshotHeaderLen) {
        HandleSnapshot(buf, n, nowMs);
    }
}

void ClientNet::Pump(uint32_t nowMs) {
    if (!TransportReady()) return;// includes a WT session still handshaking

    // Silence watchdog + hello retry (see the constants in client_net.hpp).
    if (_welcomed && nowMs - _lastRecvMs > kServerSilenceMs) {
        spdlog::warn("ClientNet: server silent for {} ms — re-greeting", nowMs - _lastRecvMs);
        _welcomed = false;
    }
    if (!_welcomed && nowMs - _lastHelloMs >= kHelloRetryMs) {
        SendHello();
        _lastHelloMs = nowMs;
    }

    PumpConditioned(
        _cond,
        _metrics,
        nowMs,
        [&](uint8_t* b, int max, uint32_t& from, uint16_t& port) {
#ifdef __EMSCRIPTEN__
            if (_useWt) return _wt.RecvFrom(b, max, from, port);
#endif
            return _socket.RecvFrom(b, max, from, port);
        },
        [&](const uint8_t* b, int n, uint32_t from, uint16_t port) { HandlePacket(b, n, from, port, nowMs); }
    );
    _metrics.pendingInputs = static_cast<int>(_pending.size());
    _metrics.lossPct = _cond.Active() ? _cond.MeasuredLossPct() : 0.0f;
}

void ClientNet::HandleSnapshot(const uint8_t* data, int len, uint32_t nowMs) {
    _lastServerTick = proto::GetU32(data + 5);
    const uint32_t ackedInputTick = proto::GetU32(data + 9);

    // RTT: this snapshot acks an input we timestamped on send. The measured
    // round trip naturally includes any emulated latency the conditioner added.
    auto sent = _inputSendMs.find(ackedInputTick);
    if (sent != _inputSendMs.end()) _metrics.FeedRtt(static_cast<float>(nowMs - sent->second));
    _inputSendMs.erase(_inputSendMs.begin(), _inputSendMs.upper_bound(ackedInputTick));

    sim::Motion serverMotion;
    serverMotion.foot = proto::GetVec3(data + 13);
    serverMotion.vy = proto::GetF32(data + 25);
    serverMotion.dashCd = data[29];
    serverMotion.dashTicks = data[30];
    _health = data[31];
    _alive = data[32] != 0;
    _shield = data[33] != 0;
    _score = data[34];
    _enemyScore = data[35];
    _enemyAlive = data[36] != 0;
    const sim::Vec3 enemyFoot = proto::GetVec3(data + 37);
    const float enemyYaw = proto::DequantizeYaw(proto::GetU16(data + 49));
    const float enemyPitch = proto::DequantizePitch(proto::GetU16(data + 51));
    _enemyShield = data[54] != 0;

    // Exact rewind-replay reconciliation: reset the full motion state to the
    // server's authoritative value as of ackedInputTick, then replay pending
    // inputs (vertical velocity and dash timers reconcile along with position).
    const sim::Vec3 predBefore = _predictedMotion.foot;// head-of-prediction before correcting
    _predictedMotion = serverMotion;
    for (auto it = _pending.begin(); it != _pending.end();) {
        if (it->first <= ackedInputTick) {
            it = _pending.erase(it);
        } else {
            if (_alive)
                _predictedMotion = sim::StepPlayer(
                    _predictedMotion,
                    it->second.forward,
                    it->second.strafe,
                    it->second.yaw,
                    it->second.jump,
                    it->second.dash
                );
            ++it;
        }
    }
    // Prediction error = how far reconciliation snapped the current predicted
    // position: ~0 on a clean link (server agrees, replay reproduces the same
    // head), spikes under loss/latency, then settles.
    const sim::Vec3 corr = { _predictedMotion.foot.x - predBefore.x,
                             _predictedMotion.foot.y - predBefore.y,
                             _predictedMotion.foot.z - predBefore.z };
    _metrics.predErr = std::sqrt(corr.x * corr.x + corr.y * corr.y + corr.z * corr.z);

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
    const uint8_t numRockets = data[55];
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

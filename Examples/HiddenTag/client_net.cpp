#include "client_net.hpp"

#include <spdlog/spdlog.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {
#if defined(_WIN32)
    bool EnsureSocketLib() {
        static bool ok = [] {
            WSADATA w;
            return WSAStartup(MAKEWORD(2, 2), &w) == 0;
        }();
        return ok;
    }
    void SetNonBlocking(SocketHandle s) {
        u_long m = 1;
        ::ioctlsocket(s, FIONBIO, &m);
    }
    void CloseSocketHandle(SocketHandle s) {
        ::closesocket(s);
    }
#else
    bool EnsureSocketLib() {
        return true;
    }
    void SetNonBlocking(SocketHandle s) {
        ::fcntl(s, F_SETFL, ::fcntl(s, F_GETFL, 0) | O_NONBLOCK);
    }
    void CloseSocketHandle(SocketHandle s) {
        ::close(s);
    }
#endif

    float Lerp(float a, float b, float t) {
        return a + (b - a) * t;
    }
}// namespace

ClientNet::~ClientNet() {
    if (_sock != kInvalidSocket) CloseSocketHandle(_sock);
}

bool ClientNet::Connect(const std::string& serverIp, uint16_t serverPort, proto::Role role) {
    if (!EnsureSocketLib()) return false;
    _sock = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (_sock == kInvalidSocket) return false;
    SetNonBlocking(_sock);

    in_addr a{};
    if (inet_pton(AF_INET, serverIp.c_str(), &a) != 1) {
        spdlog::error("ClientNet: invalid server address: {}", serverIp);
        return false;
    }
    _serverAddr = a.s_addr;
    _serverPort = htons(serverPort);
    _role = role;

    uint8_t buf[proto::kClientHelloLen];
    proto::PutU32(buf, proto::kMagic);
    buf[4] = static_cast<uint8_t>(proto::PacketType::ClientHello);
    buf[5] = static_cast<uint8_t>(role);
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_addr.s_addr = _serverAddr;
    to.sin_port = _serverPort;
    ::sendto(_sock, reinterpret_cast<const char*>(buf), sizeof(buf), 0, reinterpret_cast<sockaddr*>(&to), sizeof(to));
    return true;
}

void ClientNet::SubmitInput(uint32_t tick, float dx, float dy) {
    if (_sock == kInvalidSocket) return;

    _predictedPos = sim::Step(_predictedPos, dx, dy);

    uint8_t buf[proto::kClientInputLen];
    proto::PutU32(buf, proto::kMagic);
    buf[4] = static_cast<uint8_t>(proto::PacketType::ClientInput);
    proto::PutU32(buf + 5, tick);
    buf[9] = static_cast<uint8_t>(proto::QuantizeAxis(dx));
    buf[10] = static_cast<uint8_t>(proto::QuantizeAxis(dy));
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_addr.s_addr = _serverAddr;
    to.sin_port = _serverPort;
    ::sendto(_sock, reinterpret_cast<const char*>(buf), sizeof(buf), 0, reinterpret_cast<sockaddr*>(&to), sizeof(to));
}

void ClientNet::Pump(uint32_t nowMs) {
    if (_sock == kInvalidSocket) return;
    uint8_t buf[256];
    for (;;) {
        sockaddr_in from{};
        socklen_t fromLen = sizeof(from);
        int n = ::recvfrom(
            _sock,
            reinterpret_cast<char*>(buf),
            static_cast<int>(sizeof(buf)),
            0,
            reinterpret_cast<sockaddr*>(&from),
            &fromLen
        );
        if (n <= 0) break;
        if (from.sin_addr.s_addr != _serverAddr || from.sin_port != _serverPort) continue;
        if (n < 5 || proto::GetU32(buf) != proto::kMagic) continue;
        auto type = static_cast<proto::PacketType>(buf[4]);

        if (type == proto::PacketType::ServerWelcome && n >= proto::kServerWelcomeLen) {
            _welcomed = true;
            spdlog::info("ClientNet: welcomed, entity id {}", buf[5]);
        } else if (type == proto::PacketType::ServerSnapshot && n >= proto::kServerSnapshotHeaderLen) {
            HandleSnapshot(buf, n, nowMs);
        }
    }
}

void ClientNet::HandleSnapshot(const uint8_t* data, int len, uint32_t nowMs) {
    sim::Vec2 serverOwnPos{ proto::GetF32(data + 13), proto::GetF32(data + 17) };
    uint8_t otherCount = data[21];

    // Error-smoothing reconciliation (see class comment): blend most of the
    // way there normally, hard-snap only on a large divergence.
    float ddx = serverOwnPos.x - _predictedPos.x;
    float ddy = serverOwnPos.y - _predictedPos.y;
    constexpr float kHardSnapDistSq = 150.0f * 150.0f;
    constexpr float kSmoothFactor = 0.15f;
    if ((ddx * ddx + ddy * ddy) > kHardSnapDistSq) {
        _predictedPos = serverOwnPos;
    } else {
        _predictedPos.x += ddx * kSmoothFactor;
        _predictedPos.y += ddy * kSmoothFactor;
    }

    if (otherCount == 0 || len < proto::kServerSnapshotHeaderLen + proto::kSnapshotEntryLen) {
        // Not currently visible to us — don't keep a stale position around;
        // that would leak information the server deliberately withheld.
        _haveRemoteA = _haveRemoteB = false;
        return;
    }

    const uint8_t* entry = data + proto::kServerSnapshotHeaderLen;
    sim::Vec2 pos{ proto::GetF32(entry + 1), proto::GetF32(entry + 5) };
    _remoteA = _remoteB;
    _haveRemoteA = _haveRemoteB;
    _remoteB = { pos, nowMs };
    _haveRemoteB = true;
}

sim::Vec2 ClientNet::GetRemotePos(uint32_t nowMs) const {
    if (!_haveRemoteB) return {};
    if (!_haveRemoteA || _remoteB.recvMs <= _remoteA.recvMs) return _remoteB.pos;

    uint32_t renderTime = (nowMs > kInterpDelayMs) ? nowMs - kInterpDelayMs : 0;
    float span = static_cast<float>(_remoteB.recvMs - _remoteA.recvMs);
    float t = static_cast<float>(renderTime) - static_cast<float>(_remoteA.recvMs);
    t = (t < 0.0f) ? 0.0f : (t > span ? span : t);
    t /= span;
    return { Lerp(_remoteA.pos.x, _remoteB.pos.x, t), Lerp(_remoteA.pos.y, _remoteB.pos.y, t) };
}

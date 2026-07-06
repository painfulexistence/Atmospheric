#include "authority.hpp"

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
    using SocketHandle = uintptr_t;
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
    using SocketHandle = int;
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
}// namespace

HiddenTagAuthority::~HiddenTagAuthority() {
    Shutdown();
}

bool HiddenTagAuthority::Bind(uint16_t port) {
    if (_sock != kInvalidSocket) Shutdown();
    if (!EnsureSocketLib()) {
        spdlog::error("HiddenTagAuthority: socket subsystem init failed");
        return false;
    }
    _sock = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (_sock == kInvalidSocket) {
        spdlog::error("HiddenTagAuthority: socket() failed");
        return false;
    }
    SetNonBlocking(_sock);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (::bind(_sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        spdlog::error("HiddenTagAuthority: bind() failed on port {} (port in use?)", port);
        CloseSocketHandle(_sock);
        _sock = kInvalidSocket;
        return false;
    }
    if (port == 0) {
        sockaddr_in bound{};
        socklen_t boundLen = sizeof(bound);
        if (::getsockname(_sock, reinterpret_cast<sockaddr*>(&bound), &boundLen) == 0) port = ntohs(bound.sin_port);
    }
    _port = port;
    _serverTick = 0;
    _tickScheduled = false;
    ResetRound();
    spdlog::info("HiddenTagAuthority: bound on UDP :{}", _port);
    return true;
}

void HiddenTagAuthority::Shutdown() {
    if (_sock != kInvalidSocket) {
        CloseSocketHandle(_sock);
        _sock = kInvalidSocket;
    }
    _slots[0] = ClientSlot{};
    _slots[1] = ClientSlot{};
}

void HiddenTagAuthority::ResetRound() {
    _slots[static_cast<int>(proto::Role::Seeker)].pos = { 60.0f, 60.0f };
    _slots[static_cast<int>(proto::Role::Hider)].pos = { sim::kArenaW - 60.0f, sim::kArenaH - 60.0f };
    spdlog::info("HiddenTagAuthority: round reset");
}

void HiddenTagAuthority::SendTo(uint32_t addr, uint16_t port, const uint8_t* data, int len) {
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_addr.s_addr = addr;
    to.sin_port = port;
    ::sendto(_sock, reinterpret_cast<const char*>(data), len, 0, reinterpret_cast<sockaddr*>(&to), sizeof(to));
}

void HiddenTagAuthority::HandlePacket(const uint8_t* data, int len, uint32_t fromAddr, uint16_t fromPort) {
    if (len < 5 || proto::GetU32(data) != proto::kMagic) return;
    auto type = static_cast<proto::PacketType>(data[4]);

    if (type == proto::PacketType::ClientHello && len >= proto::kClientHelloLen) {
        auto role = static_cast<proto::Role>(data[5]);
        if (role != proto::Role::Seeker && role != proto::Role::Hider) return;
        ClientSlot& slot = _slots[static_cast<int>(role)];
        slot.connected = true;
        slot.addr = fromAddr;
        slot.port = fromPort;
        spdlog::info("HiddenTagAuthority: {} connected", role == proto::Role::Seeker ? "Seeker" : "Hider");

        uint8_t buf[proto::kServerWelcomeLen];
        proto::PutU32(buf, proto::kMagic);
        buf[4] = static_cast<uint8_t>(proto::PacketType::ServerWelcome);
        buf[5] = (role == proto::Role::Seeker) ? proto::kSeekerEntityId : proto::kHiderEntityId;
        proto::PutF32(buf + 6, sim::kTickRateHz);
        SendTo(fromAddr, fromPort, buf, sizeof(buf));
        return;
    }

    if (type == proto::PacketType::ClientInput && len >= proto::kClientInputLen) {
        for (auto& slot : _slots) {
            if (!slot.connected || slot.addr != fromAddr || slot.port != fromPort) continue;
            uint32_t tick = proto::GetU32(data + 5);
            if (tick <= slot.lastInputTick && slot.lastInputTick != 0) continue;// stale/duplicate
            slot.lastInputTick = tick;
            slot.dx = proto::DequantizeAxis(static_cast<int8_t>(data[9]));
            slot.dy = proto::DequantizeAxis(static_cast<int8_t>(data[10]));
            return;
        }
    }
}

void HiddenTagAuthority::Tick() {
    ClientSlot& seeker = _slots[static_cast<int>(proto::Role::Seeker)];
    ClientSlot& hider = _slots[static_cast<int>(proto::Role::Hider)];

    if (seeker.connected) seeker.pos = sim::Step(seeker.pos, seeker.dx, seeker.dy);
    if (hider.connected) hider.pos = sim::Step(hider.pos, hider.dx, hider.dy);

    if (seeker.connected && hider.connected && sim::IsTagged(seeker.pos, hider.pos)) {
        spdlog::info("HiddenTagAuthority: Seeker tagged Hider!");
        ResetRound();
    }

    // Build and send one snapshot per connected client — visibility
    // filtering happens here, not on the client, which is what makes it
    // real: an entity the client isn't allowed to see is simply never in
    // the bytes sent to it.
    for (int i = 0; i < 2; i++) {
        ClientSlot& self = _slots[i];
        if (!self.connected) continue;
        auto role = static_cast<proto::Role>(i);
        const ClientSlot& other = _slots[1 - i];

        bool includeOther = other.connected
                            && (role == proto::Role::Hider// Hider always sees the Seeker
                                || sim::SeekerCanSeeHider(self.pos, other.pos));

        uint8_t buf[proto::kServerSnapshotHeaderLen + proto::kSnapshotEntryLen * proto::kMaxVisibleEntities];
        proto::PutU32(buf, proto::kMagic);
        buf[4] = static_cast<uint8_t>(proto::PacketType::ServerSnapshot);
        proto::PutU32(buf + 5, _serverTick);
        proto::PutU32(buf + 9, self.lastInputTick);
        proto::PutF32(buf + 13, self.pos.x);
        proto::PutF32(buf + 17, self.pos.y);
        buf[21] = includeOther ? 1 : 0;
        int off = proto::kServerSnapshotHeaderLen;
        if (includeOther) {
            buf[off++] = (role == proto::Role::Seeker) ? proto::kHiderEntityId : proto::kSeekerEntityId;
            proto::PutF32(buf + off, other.pos.x);
            off += 4;
            proto::PutF32(buf + off, other.pos.y);
            off += 4;
        }
        SendTo(self.addr, self.port, buf, off);
    }
}

void HiddenTagAuthority::Pump() {
    if (_sock == kInvalidSocket) return;

    uint8_t buf[128];
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
        HandlePacket(buf, n, from.sin_addr.s_addr, from.sin_port);
    }

    const auto now = std::chrono::steady_clock::now();
    if (!_tickScheduled) {
        const auto tickDur = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(sim::kTickDt)
        );
        _nextTick = now + tickDur;
        _tickScheduled = true;
    }
    if (now >= _nextTick) {
        Tick();
        _serverTick++;
        const auto tickDur = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(sim::kTickDt)
        );
        _nextTick += tickDur;
    }
}

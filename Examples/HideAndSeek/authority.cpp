#include "authority.hpp"

#include <spdlog/spdlog.h>

HideAndSeekAuthority::~HideAndSeekAuthority() {
    Shutdown();
}

bool HideAndSeekAuthority::Bind(uint16_t port) {
    if (_socket.IsOpen()) Shutdown();
    if (!_socket.Open(port)) {
        spdlog::error("HideAndSeekAuthority: bind() failed on port {} (port in use?)", port);
        return false;
    }
    _serverTick = 0;
    _tickScheduled = false;
    ResetRound();
    spdlog::info("HideAndSeekAuthority: bound on UDP :{}", _socket.BoundPort());
    return true;
}

void HideAndSeekAuthority::Shutdown() {
    _socket.Close();
    _slots[0] = ClientSlot{};
    _slots[1] = ClientSlot{};
}

void HideAndSeekAuthority::ResetRound() {
    _slots[static_cast<int>(proto::Role::Seeker)].pos = { 60.0f, 60.0f };
    _slots[static_cast<int>(proto::Role::Hider)].pos = { sim::kArenaW - 60.0f, sim::kArenaH - 60.0f };
    spdlog::info("HideAndSeekAuthority: round reset");
}

void HideAndSeekAuthority::SendTo(uint32_t addr, uint16_t port, const uint8_t* data, int len) {
    _socket.SendTo(addr, port, data, len);
}

void HideAndSeekAuthority::HandlePacket(const uint8_t* data, int len, uint32_t fromAddr, uint16_t fromPort) {
    if (len < 5 || proto::GetU32(data) != proto::kMagic) return;
    auto type = static_cast<proto::PacketType>(data[4]);

    if (type == proto::PacketType::ClientHello && len >= proto::kClientHelloLen) {
        auto role = static_cast<proto::Role>(data[5]);
        if (role != proto::Role::Seeker && role != proto::Role::Hider) return;
        ClientSlot& slot = _slots[static_cast<int>(role)];
        slot.connected = true;
        slot.addr = fromAddr;
        slot.port = fromPort;
        spdlog::info("HideAndSeekAuthority: {} connected", role == proto::Role::Seeker ? "Seeker" : "Hider");

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

void HideAndSeekAuthority::Tick() {
    ClientSlot& seeker = _slots[static_cast<int>(proto::Role::Seeker)];
    ClientSlot& hider = _slots[static_cast<int>(proto::Role::Hider)];

    if (seeker.connected) seeker.pos = sim::Step(seeker.pos, seeker.dx, seeker.dy);
    if (hider.connected) hider.pos = sim::Step(hider.pos, hider.dx, hider.dy);

    if (seeker.connected && hider.connected && sim::IsTagged(seeker.pos, hider.pos)) {
        spdlog::info("HideAndSeekAuthority: Seeker tagged Hider!");
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

void HideAndSeekAuthority::Pump() {
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

// HiddenTagServer — authoritative dedicated server for a 1 Seeker vs 1 Hider
// hide-and-tag game. Demonstrates the three properties deterministic lockstep
// (see MultiplayerSandbox) can't give an asymmetric, hidden-information game:
//
//   1. Hidden information is never sent, not just render-hidden: the Hider's
//      position is only included in the Seeker's snapshot when within vision
//      radius. There is nothing in the Seeker's memory to read out of even
//      with a memory-scanning cheat tool — the server never sent it.
//   2. The server, not the client, owns movement speed: ClientInput carries
//      only a direction, and the server multiplies by its own fixed speed
//      and tick delta. A modified client reporting an inflated direction
//      gains nothing (sim::Step clamps direction to unit length first).
//   3. No cross-machine determinism is required at all — a single process
//      (this one) is the sole simulator, which is what makes properties 1
//      and 2 possible in the first place.
//
// A plain loop, not an Application: this server has no GameObject/Scene/
// rendering needs, so the AddSubsystem<T>/headless Application machinery
// would only add an unproven dependency (see Examples/RelayServer's
// RelayServer-vs-RelayServerApp split for the same reasoning applied there).
//
//   ./HiddenTagServer [--port <n>]     (default 9100)
#include "protocol.hpp"
#include "sim_common.hpp"

#include <spdlog/spdlog.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <thread>

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

    std::atomic<bool> g_stopRequested{ false };

#if defined(_WIN32)
    using SocketHandle = uintptr_t;
    constexpr SocketHandle kInvalidSocket = SocketHandle(~uintptr_t(0));
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
    void CloseSocket(SocketHandle s) {
        ::closesocket(s);
    }
#else
    using SocketHandle = int;
    constexpr SocketHandle kInvalidSocket = SocketHandle(-1);
    bool EnsureSocketLib() {
        return true;
    }
    void SetNonBlocking(SocketHandle s) {
        ::fcntl(s, F_SETFL, ::fcntl(s, F_GETFL, 0) | O_NONBLOCK);
    }
    void CloseSocket(SocketHandle s) {
        ::close(s);
    }
#endif

    struct ClientSlot {
        bool connected = false;
        uint32_t addr = 0;
        uint16_t port = 0;
        sim::Vec2 pos;
        float dx = 0.0f, dy = 0.0f;
        uint32_t lastInputTick = 0;
    };

    ClientSlot g_slots[2];// indexed by proto::Role

    void ResetRound() {
        g_slots[static_cast<int>(proto::Role::Seeker)].pos = { 60.0f, 60.0f };
        g_slots[static_cast<int>(proto::Role::Hider)].pos = { sim::kArenaW - 60.0f, sim::kArenaH - 60.0f };
        spdlog::info("HiddenTagServer: round reset");
    }

    void SendTo(SocketHandle sock, uint32_t addr, uint16_t port, const uint8_t* data, int len) {
        sockaddr_in to{};
        to.sin_family = AF_INET;
        to.sin_addr.s_addr = addr;
        to.sin_port = port;
        ::sendto(sock, reinterpret_cast<const char*>(data), len, 0, reinterpret_cast<sockaddr*>(&to), sizeof(to));
    }

    void HandlePacket(SocketHandle sock, const uint8_t* data, int len, uint32_t fromAddr, uint16_t fromPort) {
        if (len < 5 || proto::GetU32(data) != proto::kMagic) return;
        auto type = static_cast<proto::PacketType>(data[4]);

        if (type == proto::PacketType::ClientHello && len >= proto::kClientHelloLen) {
            auto role = static_cast<proto::Role>(data[5]);
            if (role != proto::Role::Seeker && role != proto::Role::Hider) return;
            ClientSlot& slot = g_slots[static_cast<int>(role)];
            slot.connected = true;
            slot.addr = fromAddr;
            slot.port = fromPort;
            spdlog::info("HiddenTagServer: {} connected", role == proto::Role::Seeker ? "Seeker" : "Hider");

            uint8_t buf[proto::kServerWelcomeLen];
            proto::PutU32(buf, proto::kMagic);
            buf[4] = static_cast<uint8_t>(proto::PacketType::ServerWelcome);
            buf[5] = (role == proto::Role::Seeker) ? proto::kSeekerEntityId : proto::kHiderEntityId;
            proto::PutF32(buf + 6, sim::kTickRateHz);
            SendTo(sock, fromAddr, fromPort, buf, sizeof(buf));
            return;
        }

        if (type == proto::PacketType::ClientInput && len >= proto::kClientInputLen) {
            for (auto& slot : g_slots) {
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

    void Tick(SocketHandle sock, uint32_t serverTick) {
        ClientSlot& seeker = g_slots[static_cast<int>(proto::Role::Seeker)];
        ClientSlot& hider = g_slots[static_cast<int>(proto::Role::Hider)];

        if (seeker.connected) seeker.pos = sim::Step(seeker.pos, seeker.dx, seeker.dy);
        if (hider.connected) hider.pos = sim::Step(hider.pos, hider.dx, hider.dy);

        if (seeker.connected && hider.connected && sim::IsTagged(seeker.pos, hider.pos)) {
            spdlog::info("HiddenTagServer: Seeker tagged Hider!");
            ResetRound();
        }

        // Build and send one snapshot per connected client — visibility
        // filtering happens here, not on the client, which is what makes it
        // real: an entity the client isn't allowed to see is simply never in
        // the bytes sent to it.
        for (int i = 0; i < 2; i++) {
            ClientSlot& self = g_slots[i];
            if (!self.connected) continue;
            auto role = static_cast<proto::Role>(i);
            const ClientSlot& other = g_slots[1 - i];

            bool includeOther = other.connected
                                && (role == proto::Role::Hider// Hider always sees the Seeker
                                    || sim::SeekerCanSeeHider(self.pos, other.pos));

            uint8_t buf[proto::kServerSnapshotHeaderLen + proto::kSnapshotEntryLen * proto::kMaxVisibleEntities];
            proto::PutU32(buf, proto::kMagic);
            buf[4] = static_cast<uint8_t>(proto::PacketType::ServerSnapshot);
            proto::PutU32(buf + 5, serverTick);
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
            SendTo(sock, self.addr, self.port, buf, off);
        }
    }

}// namespace

int main(int argc, char* argv[]) {
    uint16_t port = 9100;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc) port = static_cast<uint16_t>(std::atoi(argv[++i]));
    }

    std::signal(SIGINT, [](int) { g_stopRequested = true; });
#ifdef SIGTERM
    std::signal(SIGTERM, [](int) { g_stopRequested = true; });
#endif

    if (!EnsureSocketLib()) {
        spdlog::error("HiddenTagServer: socket subsystem init failed");
        return 1;
    }
    SocketHandle sock = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == kInvalidSocket) {
        spdlog::error("HiddenTagServer: socket() failed");
        return 1;
    }
    SetNonBlocking(sock);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (::bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        spdlog::error("HiddenTagServer: bind() failed on port {} (port in use?)", port);
        CloseSocket(sock);
        return 1;
    }

    ResetRound();
    spdlog::info("HiddenTagServer: listening on UDP :{} (Ctrl+C to stop)", port);

    const auto tickDur =
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(sim::kTickDt));
    auto nextTick = std::chrono::steady_clock::now() + tickDur;
    uint32_t serverTick = 0;

    while (!g_stopRequested) {
        uint8_t buf[128];
        for (;;) {
            sockaddr_in from{};
            socklen_t fromLen = sizeof(from);
            int n = ::recvfrom(
                sock,
                reinterpret_cast<char*>(buf),
                static_cast<int>(sizeof(buf)),
                0,
                reinterpret_cast<sockaddr*>(&from),
                &fromLen
            );
            if (n <= 0) break;
            HandlePacket(sock, buf, n, from.sin_addr.s_addr, from.sin_port);
        }

        if (std::chrono::steady_clock::now() >= nextTick) {
            Tick(sock, serverTick++);
            nextTick += tickDur;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    CloseSocket(sock);
    spdlog::info("HiddenTagServer: shut down");
    return 0;
}

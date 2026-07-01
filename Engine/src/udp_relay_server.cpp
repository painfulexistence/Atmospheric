#include "udp_relay_server.hpp"

#ifndef __EMSCRIPTEN__

#include "application.hpp"
#include <imgui.h>
#include <spdlog/spdlog.h>
#include <cstring>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
#else
#  include <arpa/inet.h>
#  include <fcntl.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <unistd.h>
#endif

namespace {

#if defined(_WIN32)
using SockH = uintptr_t;
inline bool  EnsureSocketLib() {
    static bool ok = [] { WSADATA w; return WSAStartup(MAKEWORD(2,2), &w) == 0; }();
    return ok;
}
inline void  SetNonBlocking(SockH s) { u_long m=1; ::ioctlsocket(s, FIONBIO, &m); }
inline void  CloseSocket(SockH s)    { ::closesocket(s); }
#else
using SockH = int;
inline bool  EnsureSocketLib()       { return true; }
inline void  SetNonBlocking(SockH s) { ::fcntl(s, F_SETFL, ::fcntl(s, F_GETFL, 0) | O_NONBLOCK); }
inline void  CloseSocket(SockH s)    { ::close(s); }
#endif

inline uint32_t GetU32LE(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1])<<8 | uint32_t(p[2])<<16 | uint32_t(p[3])<<24;
}

} // namespace

void UdpRelayServer::Init(Application* app) {
    Server::Init(app);
    spdlog::info("UdpRelayServer: initialized");
}

bool UdpRelayServer::Start(uint16_t port) {
    if (_running) Stop();
    if (!EnsureSocketLib()) {
        spdlog::error("UdpRelayServer: socket subsystem init failed");
        return false;
    }
    _sock = static_cast<SocketHandle>(::socket(AF_INET, SOCK_DGRAM, 0));
    if (_sock == kInvalidSocket) {
        spdlog::error("UdpRelayServer: socket() failed");
        return false;
    }
    SetNonBlocking(static_cast<SockH>(_sock));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);
    if (::bind(_sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        spdlog::error("UdpRelayServer: bind() failed on port {} (port in use?)", port);
        CloseSocket(static_cast<SockH>(_sock));
        _sock = kInvalidSocket;
        return false;
    }
    _boundPort = port;
    _running   = true;
    spdlog::info("UdpRelayServer: listening on UDP port {}", port);
    return true;
}

void UdpRelayServer::Stop() {
    if (_sock != kInvalidSocket) {
        CloseSocket(static_cast<SockH>(_sock));
        _sock = kInvalidSocket;
    }
    _rooms.clear();
    _running   = false;
    _boundPort = 0;
    spdlog::info("UdpRelayServer: stopped");
}

void UdpRelayServer::Process(float dt) {
    if (!_running) return;
    uint32_t prevMs = _totalMs;
    _totalMs += uint32_t(dt * 1000.f);
    _pump();
    // Evict once per second
    if (_totalMs / 1000 != prevMs / 1000)
        _evictStaleRooms();
}

void UdpRelayServer::DrawImGui(float /*dt*/) {
#ifndef NDEBUG
    if (!ImGui::CollapsingHeader("UdpRelayServer")) return;
    ImGui::Text("Running  : %s", _running ? "yes" : "no");
    ImGui::Text("Port     : %u", _boundPort);
    ImGui::Text("Rooms    : %d", RoomCount());
    if (ImGui::TreeNode("Rooms##relay")) {
        for (auto& [id, room] : _rooms) {
            ImGui::Text("Room %u  idle %u ms", id, _totalMs - room.lastActivityMs);
            for (int i = 0; i < 2; i++) {
                const auto& p = room.peers[i];
                if (p.valid) {
                    in_addr a{};
                    a.s_addr = p.addr;
                    char ipbuf[INET_ADDRSTRLEN] = "?";
                    inet_ntop(AF_INET, &a, ipbuf, sizeof(ipbuf));
                    ImGui::Text("  peer[%d] %s:%u", i, ipbuf, ntohs(p.port));
                } else {
                    ImGui::Text("  peer[%d] <waiting>", i);
                }
            }
        }
        ImGui::TreePop();
    }
#endif
}

void UdpRelayServer::_pump() {
    // Wire format: [roomId: uint32_t LE][LockstepNet payload ...]
    // Minimum meaningful packet: 4 (roomId) + 1 (any payload byte) = 5 bytes.
    uint8_t buf[1504]; // 4-byte header + 1500-byte max UDP payload
    for (;;) {
        sockaddr_in from{};
        socklen_t   fromLen = sizeof(from);
        int n = ::recvfrom(_sock, reinterpret_cast<char*>(buf), int(sizeof(buf)), 0,
                           reinterpret_cast<sockaddr*>(&from), &fromLen);
        if (n <= 0) break;
        if (n < 5) continue; // too short: no payload after roomId

        const uint32_t roomId     = GetU32LE(buf);
        const uint8_t* payload    = buf + 4;
        const int      payloadLen = n - 4;

        Room& room = _rooms[roomId];
        room.lastActivityMs = _totalMs;

        const uint32_t senderAddr = from.sin_addr.s_addr;
        const uint16_t senderPort = from.sin_port; // already network-byte-order

        // Find which slot this sender occupies (or assign one).
        int senderIdx = -1;
        int otherIdx  = -1;
        for (int i = 0; i < 2; i++) {
            if (room.peers[i].valid &&
                room.peers[i].addr == senderAddr &&
                room.peers[i].port == senderPort) {
                senderIdx = i;
                otherIdx  = 1 - i;
                break;
            }
        }
        if (senderIdx < 0) {
            // New sender — register in the first empty slot.
            for (int i = 0; i < 2; i++) {
                if (!room.peers[i].valid) {
                    room.peers[i] = {senderAddr, senderPort, true};
                    senderIdx = i;
                    otherIdx  = 1 - i;
                    break;
                }
            }
        }
        if (senderIdx < 0) continue; // room full, unknown sender — drop

        // Forward bare payload to the other peer (if already registered).
        if (otherIdx >= 0 && room.peers[otherIdx].valid)
            _forwardTo(room.peers[otherIdx], payload, payloadLen);
    }
}

void UdpRelayServer::_evictStaleRooms() {
    for (auto it = _rooms.begin(); it != _rooms.end();) {
        if (_totalMs - it->second.lastActivityMs > kRoomTimeoutMs) {
            spdlog::debug("UdpRelayServer: evicting idle room {}", it->first);
            it = _rooms.erase(it);
        } else {
            ++it;
        }
    }
}

void UdpRelayServer::_forwardTo(const Peer& dst, const uint8_t* payload, int len) {
    if (!dst.valid || _sock == kInvalidSocket) return;
    sockaddr_in to{};
    to.sin_family      = AF_INET;
    to.sin_addr.s_addr = dst.addr;
    to.sin_port        = dst.port;
    ::sendto(_sock, reinterpret_cast<const char*>(payload), len, 0,
             reinterpret_cast<sockaddr*>(&to), sizeof(to));
}

#endif // !__EMSCRIPTEN__

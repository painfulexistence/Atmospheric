#include "net_lockstep.hpp"
#include <cstring>

#ifndef __EMSCRIPTEN__
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
#endif

namespace {
// ── Platform shims (POSIX sockets vs. Winsock2) ───────────────────────────
#ifndef __EMSCRIPTEN__
#if defined(_WIN32)
inline bool EnsureSocketLib() {
    static bool ok = [] {
        WSADATA wsa;
        return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
    }();
    return ok;
}
inline void SetNonBlocking(SocketHandle s) {
    u_long mode = 1;
    ::ioctlsocket(s, FIONBIO, &mode);
}
inline void CloseSocket(SocketHandle s) {
    ::closesocket(s);
}
#else
inline bool EnsureSocketLib() {
    return true;
}
inline void SetNonBlocking(SocketHandle s) {
    int flags = ::fcntl(s, F_GETFL, 0);
    ::fcntl(s, F_SETFL, flags | O_NONBLOCK);
}
inline void CloseSocket(SocketHandle s) {
    ::close(s);
}
#endif
#endif

constexpr uint32_t MAGIC = 0x4e4c4b31;// "NLK1"
constexpr uint8_t PKT_HELLO = 1;
constexpr uint8_t PKT_WELCOME = 2;
constexpr uint8_t PKT_INPUT = 3;
constexpr uint8_t PKT_CHECK = 4;
constexpr uint8_t PKT_PING = 5;
constexpr uint8_t PKT_PONG = 6;
constexpr int MAX_REDUNDANT_INPUTS = 32;

void PutU32(uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v);
    p[1] = uint8_t(v >> 8);
    p[2] = uint8_t(v >> 16);
    p[3] = uint8_t(v >> 24);
}
uint32_t GetU32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
}

void LockstepNet::StartSolo(uint32_t s) {
    mode = Mode::Solo;
    state = State::Running;
    seed = s;
    localPlayer = 0;
    inputDelay = 0;
}

#ifndef __EMSCRIPTEN__

bool LockstepNet::OpenSocket(uint16_t bindPort) {
    if (!EnsureSocketLib()) {
        error = "socket subsystem init failed";
        return false;
    }
    sock = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == kInvalidSocket) {
        error = "socket() failed";
        return false;
    }
    SetNonBlocking(sock);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(bindPort);
    if (::bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        error = "bind() failed (port in use?)";
        CloseSocket(sock);
        sock = kInvalidSocket;
        return false;
    }
    return true;
}

bool LockstepNet::StartHost(uint16_t port, uint32_t s, int delay) {
    if (!OpenSocket(port)) {
        state = State::Failed;
        return false;
    }
    mode = Mode::Host;
    state = State::Connecting;
    seed = s;
    inputDelay = delay;
    localPlayer = 0;
    return true;
}

bool LockstepNet::StartClient(const std::string& ip, uint16_t port) {
    if (!OpenSocket(0)) {
        state = State::Failed;
        return false;
    }
    in_addr a{};
    if (inet_pton(AF_INET, ip.c_str(), &a) != 1) {
        error = "invalid host address: " + ip;
        state = State::Failed;
        return false;
    }
    mode = Mode::Client;
    state = State::Connecting;
    localPlayer = 1;
    peerAddr = a.s_addr;
    peerPort = htons(port);
    havePeer = true;
    return true;
}

void LockstepNet::Shutdown() {
    if (sock != kInvalidSocket) {
        CloseSocket(sock);
        sock = kInvalidSocket;
    }
    state      = State::Idle;
    havePeer   = false;
    peerAddr   = 0;
    peerPort   = 0;
    useRelay   = false;
    relayRoomId = 0;
}

bool LockstepNet::StartRelayHost(const std::string& relayIp, uint16_t relayPort,
                                   uint32_t roomId, uint32_t s, int delay) {
    if (!OpenSocket(0)) { state = State::Failed; return false; }
    in_addr a{};
    if (inet_pton(AF_INET, relayIp.c_str(), &a) != 1) {
        error = "invalid relay address: " + relayIp;
        state = State::Failed;
        return false;
    }
    mode        = Mode::Host;
    state       = State::Connecting;
    seed        = s;
    inputDelay  = delay;
    localPlayer = 0;
    peerAddr    = a.s_addr;
    peerPort    = htons(relayPort);
    havePeer    = true; // always send to relay from the start
    useRelay    = true;
    relayRoomId = roomId;
    return true;
}

bool LockstepNet::StartRelayClient(const std::string& relayIp, uint16_t relayPort,
                                    uint32_t roomId) {
    if (!OpenSocket(0)) { state = State::Failed; return false; }
    in_addr a{};
    if (inet_pton(AF_INET, relayIp.c_str(), &a) != 1) {
        error = "invalid relay address: " + relayIp;
        state = State::Failed;
        return false;
    }
    mode        = Mode::Client;
    state       = State::Connecting;
    localPlayer = 1;
    peerAddr    = a.s_addr;
    peerPort    = htons(relayPort);
    havePeer    = true;
    useRelay    = true;
    relayRoomId = roomId;
    return true;
}

void LockstepNet::SendRaw(const uint8_t* data, int len) {
    if (sock == kInvalidSocket || !havePeer) return;
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_addr.s_addr = peerAddr;
    to.sin_port = peerPort;
    if (useRelay) {
        // Prepend [roomId: uint32_t LE] so the relay can route the packet.
        uint8_t relayBuf[4 + 1500];
        if (len > 1500) return;
        PutU32(relayBuf, relayRoomId);
        std::memcpy(relayBuf + 4, data, len);
        ::sendto(sock, reinterpret_cast<const char*>(relayBuf), 4 + len, 0,
                 reinterpret_cast<sockaddr*>(&to), sizeof(to));
    } else {
        ::sendto(sock, reinterpret_cast<const char*>(data), len, 0,
                 reinterpret_cast<sockaddr*>(&to), sizeof(to));
    }
}

void LockstepNet::SendHello(uint32_t nowMs) {
    if (nowMs - lastHelloMs < 200) return;
    lastHelloMs = nowMs;
    uint8_t buf[5];
    PutU32(buf, MAGIC);
    buf[4] = PKT_HELLO;
    SendRaw(buf, 5);
}

void LockstepNet::SendWelcome() {
    uint8_t buf[10];
    PutU32(buf, MAGIC);
    buf[4] = PKT_WELCOME;
    PutU32(buf + 5, seed);
    buf[9] = uint8_t(inputDelay);
    SendRaw(buf, 10);
}

void LockstepNet::SendInputs() {
    if (!haveLocal || state != State::Running) return;
    uint32_t first = (localTop >= uint32_t(MAX_REDUNDANT_INPUTS - 1)) ? localTop - (MAX_REDUNDANT_INPUTS - 1) : 0;
    if (peerAckedTick + 1 > first) first = peerAckedTick + 1;
    if (first > localTop) return;

    uint8_t buf[14 + MAX_REDUNDANT_INPUTS * 4];
    PutU32(buf, MAGIC);
    buf[4] = PKT_INPUT;
    PutU32(buf + 5, remoteTop);// ack: highest remote tick we've seen
    PutU32(buf + 9, first);
    uint8_t count = 0;
    int off = 14;
    for (uint32_t t = first; t <= localTop && count < MAX_REDUNDANT_INPUTS; t++) {
        auto it = localInputs.find(t);
        InputFrame f = (it != localInputs.end()) ? it->second : InputFrame{};
        buf[off++] = f.buttons;
        buf[off++] = f.spell;
        buf[off++] = uint8_t(uint16_t(f.aimQ));
        buf[off++] = uint8_t(uint16_t(f.aimQ) >> 8);
        count++;
    }
    buf[13] = count;
    SendRaw(buf, off);
}

void LockstepNet::HandlePacket(const uint8_t* data, int len, uint32_t fromAddr, uint16_t fromPort, uint32_t nowMs) {
    if (len < 5 || GetU32(data) != MAGIC) return;
    uint8_t type = data[4];

    if (mode == Mode::Host && !havePeer && type == PKT_HELLO) {
        peerAddr = fromAddr;
        peerPort = fromPort;
        havePeer = true;
        state = State::Running;
        SendWelcome();
        return;
    }
    // After pairing, ignore datagrams from anyone else
    if (!havePeer || fromAddr != peerAddr || fromPort != peerPort) return;

    switch (type) {
    case PKT_HELLO:
        if (mode == Mode::Host) {
            // In relay mode the host pre-registers the relay as its peer and
            // stays Connecting until the first forwarded HELLO arrives.
            if (useRelay && state == State::Connecting)
                state = State::Running;
            SendWelcome();
        }
        break;
    case PKT_WELCOME:
        if (mode == Mode::Client && state == State::Connecting) {
            seed = GetU32(data + 5);
            inputDelay = int(data[9]);
            state = State::Running;
        }
        break;
    case PKT_INPUT: {
        if (len < 14) break;
        uint32_t ack = GetU32(data + 5);
        uint32_t first = GetU32(data + 9);
        uint8_t count = data[13];
        if (len < 14 + int(count) * 4) break;
        if (haveLocal && ack > peerAckedTick && ack <= localTop) peerAckedTick = ack;
        for (uint8_t i = 0; i < count; i++) {
            const uint8_t* p = data + 14 + i * 4;
            uint32_t t = first + i;
            InputFrame f;
            f.buttons = p[0];
            f.spell = p[1];
            f.aimQ = int16_t(uint16_t(p[2]) | (uint16_t(p[3]) << 8));
            remoteInputs[t] = f;
            if (t > remoteTop) remoteTop = t;
        }
        break;
    }
    case PKT_CHECK: {
        if (len < 13) break;
        uint32_t t = GetU32(data + 5);
        uint32_t sum = GetU32(data + 9);
        auto it = localChecks.find(t);
        if (it != localChecks.end() && it->second != sum) desync = true;
        break;
    }
    case PKT_PING: {
        if (len < 9) break;
        uint8_t buf[9];
        PutU32(buf, MAGIC);
        buf[4] = PKT_PONG;
        PutU32(buf + 5, GetU32(data + 5));
        SendRaw(buf, 9);
        break;
    }
    case PKT_PONG: {
        if (len < 9) break;
        uint32_t sent = GetU32(data + 5);
        rttMs = int(nowMs - sent);
        break;
    }
    default: break;
    }
}

void LockstepNet::Pump(uint32_t nowMs) {
    if (mode == Mode::Solo || sock == kInvalidSocket) return;

    uint8_t buf[1500];
    for (;;) {
        sockaddr_in from{};
        socklen_t fromLen = sizeof(from);
        int n = ::recvfrom(
          sock, reinterpret_cast<char*>(buf), int(sizeof(buf)), 0, reinterpret_cast<sockaddr*>(&from), &fromLen
        );
        if (n <= 0) break;
        HandlePacket(buf, n, from.sin_addr.s_addr, from.sin_port, nowMs);
    }

    // Client always sends HELLOs while connecting.
    // Host in relay mode also sends HELLOs so the relay can register its address
    // and start forwarding the client's HELLOs back to it.
    if (state == State::Connecting && (mode == Mode::Client || useRelay)) {
        SendHello(nowMs);
    }
    if (state == State::Running) {
        SendInputs();// redundant resend every pump; tiny packets, lowest latency
        if (nowMs - lastPingMs > 1000) {
            lastPingMs = nowMs;
            uint8_t ping[9];
            PutU32(ping, MAGIC);
            ping[4] = PKT_PING;
            PutU32(ping + 5, nowMs);
            SendRaw(ping, 9);
        }
    }
}

#else// __EMSCRIPTEN__: no UDP sockets in the browser; solo mode only

bool LockstepNet::OpenSocket(uint16_t) { return false; }
bool LockstepNet::StartHost(uint16_t, uint32_t s, int) { StartSolo(s); return true; }
bool LockstepNet::StartClient(const std::string&, uint16_t) {
    error = "networking is not available in web builds";
    state = State::Failed;
    return false;
}
bool LockstepNet::StartRelayHost(const std::string&, uint16_t, uint32_t, uint32_t s, int) {
    StartSolo(s);
    return true;
}
bool LockstepNet::StartRelayClient(const std::string&, uint16_t, uint32_t) {
    error = "networking is not available in web builds";
    state = State::Failed;
    return false;
}
void LockstepNet::Shutdown()                                        { state = State::Idle; }
void LockstepNet::SendRaw(const uint8_t*, int)                      {}
void LockstepNet::SendHello(uint32_t)                               {}
void LockstepNet::SendWelcome()                                     {}
void LockstepNet::SendInputs()                                      {}
void LockstepNet::HandlePacket(const uint8_t*, int, uint32_t, uint16_t, uint32_t) {}
void LockstepNet::Pump(uint32_t)                                    {}

#endif

// --------------------------------------------------------------------------
// Mode-independent input bookkeeping
// --------------------------------------------------------------------------

void LockstepNet::SubmitLocalInput(uint32_t tick, const InputFrame& f) {
    if (haveLocal && tick <= localTop) return;
    localInputs[tick] = f;
    localTop = tick;
    haveLocal = true;
    SendInputs();
}

bool LockstepNet::HasInputs(uint32_t tick) const {
    if (mode == Mode::Solo) return true;
    bool local = localInputs.count(tick) > 0 || tick < uint32_t(inputDelay);
    bool remote = remoteInputs.count(tick) > 0 || tick < uint32_t(inputDelay);
    return local && remote;
}

InputFrame LockstepNet::GetInput(int player, uint32_t tick) const {
    const auto& map = (player == localPlayer) ? localInputs : remoteInputs;
    auto it = map.find(tick);
    if (it != map.end()) return it->second;
    return InputFrame{};// neutral (start-up ticks before inputDelay)
}

void LockstepNet::ShareChecksum(uint32_t tick, uint32_t sum) {
    localChecks[tick] = sum;
    if (mode == Mode::Solo) return;
    uint8_t buf[13];
    PutU32(buf, MAGIC);
    buf[4] = PKT_CHECK;
    PutU32(buf + 5, tick);
    PutU32(buf + 9, sum);
    SendRaw(buf, 13);
}

void LockstepNet::PruneBelow(uint32_t tick) {
    if (tick < 240) return;
    uint32_t cutoff = tick - 240;
    for (auto it = localInputs.begin(); it != localInputs.end();) {
        it = (it->first < cutoff) ? localInputs.erase(it) : std::next(it);
    }
    for (auto it = remoteInputs.begin(); it != remoteInputs.end();) {
        it = (it->first < cutoff) ? remoteInputs.erase(it) : std::next(it);
    }
    for (auto it = localChecks.begin(); it != localChecks.end();) {
        it = (it->first < cutoff) ? localChecks.erase(it) : std::next(it);
    }
}

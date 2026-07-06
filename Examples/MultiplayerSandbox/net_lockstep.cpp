#include "net_lockstep.hpp"
#include <cstring>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

namespace {
    constexpr uint32_t gmagic = 0x4e4c4b31;// "NLK1"
    constexpr uint8_t gpktHello = 1;
    constexpr uint8_t gpktWelcome = 2;
    constexpr uint8_t gpktInput = 3;
    constexpr uint8_t gpktCheck = 4;
    constexpr uint8_t gpktPing = 5;
    constexpr uint8_t gpktPong = 6;
    constexpr int gmaxRedundantInputs = 32;

    void PutU32(uint8_t* p, uint32_t v) {
        p[0] = static_cast<uint8_t>(v);
        p[1] = static_cast<uint8_t>(v >> 8);
        p[2] = static_cast<uint8_t>(v >> 16);
        p[3] = static_cast<uint8_t>(v >> 24);
    }
    uint32_t GetU32(const uint8_t* p) {
        return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16
               | static_cast<uint32_t>(p[3]) << 24;
    }
}// namespace

void LockstepNet::StartSolo(uint32_t s) {
    mode = Mode::Solo;
    state = State::Running;
    seed = s;
    localPlayer = 0;
    inputDelay = 0;
}

// ── Platform-specific transport ────────────────────────────────────────────
// Native: raw UDP sockets (POSIX / Winsock2)
// Web:    RTCDataChannel via EM_JS glue (window._rtcChannel / _rtcRecvQueue)

#ifndef __EMSCRIPTEN__

bool LockstepNet::OpenSocket(uint16_t bindPort) {
    if (!_socket.Open(bindPort)) {
        error = "socket open/bind failed (port in use?)";
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
    if (!UdpSocket::Resolve(ip, port, peerAddr, peerPort)) {
        error = "invalid host address: " + ip;
        state = State::Failed;
        return false;
    }
    mode = Mode::Client;
    state = State::Connecting;
    localPlayer = 1;
    havePeer = true;
    return true;
}

void LockstepNet::Shutdown() {
    _socket.Close();
    state = State::Idle;
    havePeer = false;
    peerAddr = 0;
    peerPort = 0;
    useRelay = false;
}

bool LockstepNet::StartRelayHost(
    const std::string& relayIp, uint16_t relayPort, uint32_t roomId, uint32_t s, int delay
) {
    if (!OpenSocket(0)) {
        state = State::Failed;
        return false;
    }
    if (!_relayClient.Connect(relayIp, relayPort, roomId)) {
        error = "invalid relay address: " + relayIp;
        state = State::Failed;
        return false;
    }
    mode = Mode::Host;
    state = State::Connecting;
    seed = s;
    inputDelay = delay;
    localPlayer = 0;
    peerAddr = _relayClient.RelayAddr();
    peerPort = _relayClient.RelayPort();
    havePeer = true;// always send to relay from the start
    useRelay = true;
    return true;
}

bool LockstepNet::StartRelayClient(const std::string& relayIp, uint16_t relayPort, uint32_t roomId) {
    if (!OpenSocket(0)) {
        state = State::Failed;
        return false;
    }
    if (!_relayClient.Connect(relayIp, relayPort, roomId)) {
        error = "invalid relay address: " + relayIp;
        state = State::Failed;
        return false;
    }
    mode = Mode::Client;
    state = State::Connecting;
    localPlayer = 1;
    peerAddr = _relayClient.RelayAddr();
    peerPort = _relayClient.RelayPort();
    havePeer = true;
    useRelay = true;
    return true;
}

void LockstepNet::SendRaw(const uint8_t* data, int len) {
    if (!_socket.IsOpen() || !havePeer) return;
    if (useRelay) {
        _relayClient.Send(_socket, data, len);
        return;
    }
    _socket.SendTo(peerAddr, peerPort, data, len);
}

void LockstepNet::Pump(uint32_t nowMs) {
    if (mode == Mode::Solo || !_socket.IsOpen()) return;

    uint8_t buf[1500];
    for (;;) {
        uint32_t fromAddr = 0;
        uint16_t fromPort = 0;
        int n = _socket.RecvFrom(buf, static_cast<int>(sizeof(buf)), fromAddr, fromPort);
        if (n <= 0) break;
        HandlePacket(buf, n, fromAddr, fromPort, nowMs);
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
            PutU32(ping, gmagic);
            ping[4] = gpktPing;
            PutU32(ping + 5, nowMs);
            SendRaw(ping, 9);
        }
    }
}

#else// __EMSCRIPTEN__: WebRTC P2P transport via RTCDataChannel

// Send a binary frame over the WebRTC DataChannel (set up by the JS lobby).
// HEAPU8.buffer.slice() copies the bytes before async send so the WASM heap
// reallocation can't corrupt the in-flight buffer.
// clang-format off
// NOLINTBEGIN
EM_JS(void, js_rtc_send, (const uint8_t* data, int len), {
    var ch = window['_rtcChannel'];
    if (ch&& ch.readyState === 'open') {
        // AtmosphericEngine builds with -sUSE_PTHREADS=1, so the wasm heap
        // (and therefore HEAPU8.buffer) is a SharedArrayBuffer, not a plain
        // ArrayBuffer. RTCDataChannel.send() rejects SharedArrayBuffer
        // outright ("parameter 1 is not of type 'ArrayBuffer'") — and
        // SharedArrayBuffer.prototype.slice() only ever returns another
        // SharedArrayBuffer, so slicing it directly can never fix this.
        // Copying into a fresh Uint8Array gives a genuine, non-shared
        // ArrayBuffer that send() accepts.
        var copy = new Uint8Array(len);
        copy.set(HEAPU8.subarray(data, data + len));
        ch.send(copy.buffer);
    }
});
// NOLINTEND
// clang-format on

// Pull one queued message from the JS receive ring into buf.
// Returns bytes written, or 0 if the queue is empty.
// clang-format off
// NOLINTBEGIN
EM_JS(int, js_rtc_recv, (uint8_t* buf, int maxLen), {
    var q = window['_rtcRecvQueue'];
    if (!q || !q.length) return 0;
    var ab = q.shift();
    var src = new Uint8Array(ab);
    var n = Math.min(src.byteLength, maxLen);
    HEAPU8.set(src.subarray(0, n), buf);
    return n;
});
// NOLINTEND
// clang-format on

bool LockstepNet::OpenSocket(uint16_t) {
    return true;
}

bool LockstepNet::StartHost(uint16_t, uint32_t s, int delay) {
    mode = Mode::Host;
    state = State::Connecting;
    seed = s;
    inputDelay = delay;
    localPlayer = 0;
    havePeer = false;// host waits for client's PKT_HELLO over the DataChannel
    return true;
}

bool LockstepNet::StartClient(const std::string&, uint16_t) {
    // IP/port are irrelevant; the DataChannel is already open from the JS lobby.
    mode = Mode::Client;
    state = State::Connecting;
    localPlayer = 1;
    havePeer = true;
    peerAddr = 0;
    peerPort = 0;
    return true;
}

// UdpRelay is a raw-UDP fallback for native builds; it has no meaning
// over a WebRTC DataChannel, which already negotiates NAT traversal itself
// (ICE, with TURN relay as its own fallback). Fail cleanly rather than
// silently drop to solo mode.
bool LockstepNet::StartRelayHost(const std::string&, uint16_t, uint32_t, uint32_t, int) {
    error = "UDP relay mode is not applicable in web builds (WebRTC negotiates its own NAT traversal)";
    state = State::Failed;
    return false;
}

bool LockstepNet::StartRelayClient(const std::string&, uint16_t, uint32_t) {
    error = "UDP relay mode is not applicable in web builds (WebRTC negotiates its own NAT traversal)";
    state = State::Failed;
    return false;
}

void LockstepNet::Shutdown() {
    state = State::Idle;
}

void LockstepNet::SendRaw(const uint8_t* data, int len) {
    js_rtc_send(data, len);
}

void LockstepNet::Pump(uint32_t nowMs) {
    if (mode == Mode::Solo) return;

    uint8_t buf[1500];
    for (;;) {
        int n = js_rtc_recv(buf, static_cast<int>(sizeof(buf)));
        if (n <= 0) break;
        // fromAddr/fromPort are 0; HandlePacket uses the same values for
        // peerAddr/peerPort so the "ignore unknown sender" check still passes.
        HandlePacket(buf, n, 0, 0, nowMs);
    }

    if (mode == Mode::Client && state == State::Connecting) {
        SendHello(nowMs);
    }
    if (state == State::Running) {
        SendInputs();
        if (nowMs - lastPingMs > 1000) {
            lastPingMs = nowMs;
            uint8_t ping[9];
            PutU32(ping, gmagic);
            ping[4] = gpktPing;
            PutU32(ping + 5, nowMs);
            SendRaw(ping, 9);
        }
    }
}

#endif// __EMSCRIPTEN__

// ── Shared packet logic (same protocol over UDP or RTCDataChannel) ─────────

void LockstepNet::SendHello(uint32_t nowMs) {
    if (nowMs - lastHelloMs < 200) return;
    lastHelloMs = nowMs;
    uint8_t buf[5];
    PutU32(buf, gmagic);
    buf[4] = gpktHello;
    SendRaw(buf, 5);
}

void LockstepNet::SendWelcome() {
    uint8_t buf[10];
    PutU32(buf, gmagic);
    buf[4] = gpktWelcome;
    PutU32(buf + 5, seed);
    buf[9] = static_cast<uint8_t>(inputDelay);
    SendRaw(buf, 10);
}

void LockstepNet::SendInputs() {
    if (!haveLocal || state != State::Running) return;
    uint32_t first =
        (localTop >= static_cast<uint32_t>(gmaxRedundantInputs - 1)) ? localTop - (gmaxRedundantInputs - 1) : 0;
    if (peerAckedTick + 1 > first) first = peerAckedTick + 1;
    if (first > localTop) return;

    uint8_t buf[14 + gmaxRedundantInputs * 4];
    PutU32(buf, gmagic);
    buf[4] = gpktInput;
    PutU32(buf + 5, remoteTop);// ack: highest remote tick we've seen
    PutU32(buf + 9, first);
    uint8_t count = 0;
    int off = 14;
    for (uint32_t t = first; t <= localTop && count < gmaxRedundantInputs; t++) {
        auto it = localInputs.find(t);
        InputFrame f = (it != localInputs.end()) ? it->second : InputFrame{};
        buf[off++] = f.buttons;
        buf[off++] = f.spell;
        buf[off++] = static_cast<uint8_t>(static_cast<uint16_t>(f.aimQ));
        buf[off++] = static_cast<uint8_t>(static_cast<uint16_t>(f.aimQ) >> 8);
        count++;
    }
    buf[13] = count;
    SendRaw(buf, off);
}

void LockstepNet::HandlePacket(const uint8_t* data, int len, uint32_t fromAddr, uint16_t fromPort, uint32_t nowMs) {
    if (len < 5 || GetU32(data) != gmagic) return;
    uint8_t type = data[4];

    if (mode == Mode::Host && !havePeer && type == gpktHello) {
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
    case gpktHello:
        if (mode == Mode::Host) {
            // In relay mode the host pre-registers the relay as its peer and
            // stays Connecting until the first forwarded HELLO arrives.
            if (useRelay && state == State::Connecting) state = State::Running;
            SendWelcome();
        }
        break;
    case gpktWelcome:
        if (mode == Mode::Client && state == State::Connecting) {
            seed = GetU32(data + 5);
            inputDelay = static_cast<int>(data[9]);
            state = State::Running;
        }
        break;
    case gpktInput: {
        if (len < 14) break;
        uint32_t ack = GetU32(data + 5);
        uint32_t first = GetU32(data + 9);
        uint8_t count = data[13];
        if (len < 14 + static_cast<int>(count) * 4) break;
        if (haveLocal && ack > peerAckedTick && ack <= localTop) peerAckedTick = ack;
        for (uint8_t i = 0; i < count; i++) {
            const uint8_t* p = data + 14 + i * 4;
            uint32_t t = first + i;
            InputFrame f;
            f.buttons = p[0];
            f.spell = p[1];
            f.aimQ = static_cast<int16_t>(static_cast<uint16_t>(p[2]) | (static_cast<uint16_t>(p[3]) << 8));
            remoteInputs[t] = f;
            if (t > remoteTop) remoteTop = t;
        }
        break;
    }
    case gpktCheck: {
        if (len < 13) break;
        uint32_t t = GetU32(data + 5);
        uint32_t sum = GetU32(data + 9);
        auto it = localChecks.find(t);
        if (it != localChecks.end() && it->second != sum) desync = true;
        break;
    }
    case gpktPing: {
        if (len < 9) break;
        uint8_t buf[9];
        PutU32(buf, gmagic);
        buf[4] = gpktPong;
        PutU32(buf + 5, GetU32(data + 5));
        SendRaw(buf, 9);
        break;
    }
    case gpktPong: {
        if (len < 9) break;
        uint32_t sent = GetU32(data + 5);
        rttMs = static_cast<int>(nowMs - sent);
        break;
    }
    default:
        break;
    }
}

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
    bool local = localInputs.count(tick) > 0 || tick < static_cast<uint32_t>(inputDelay);
    bool remote = remoteInputs.count(tick) > 0 || tick < static_cast<uint32_t>(inputDelay);
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
    PutU32(buf, gmagic);
    buf[4] = gpktCheck;
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

#include "udp_relay.hpp"

#ifndef __EMSCRIPTEN__

#include <cstring>
#include <spdlog/spdlog.h>

namespace {

    inline uint32_t GetU32LE(const uint8_t* p) {
        return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
    }

}// namespace

bool UdpRelay::Start(uint16_t port) {
    if (_running) Stop();
    if (!_socket.Open(port)) return false;
    _running = true;
    spdlog::info("UdpRelay: listening on UDP port {}", _socket.BoundPort());
    return true;
}

void UdpRelay::Stop() {
    _socket.Close();
    _rooms.clear();
    _running = false;
    spdlog::info("UdpRelay: stopped");
}

void UdpRelay::Process(float dt) {
    if (!_running) return;
    uint32_t prevMs = _totalMs;
    _totalMs += uint32_t(dt * 1000.f);
    _pump();
    // Evict once per second
    if (_totalMs / 1000 != prevMs / 1000) {
        _evictStaleRooms();
        _evictStaleIpBudgets();
    }
}

void UdpRelay::_pump() {
    // Wire format: [roomId: uint32_t LE][LockstepNet payload ...]
    // Minimum meaningful packet: 4 (roomId) + 1 (any payload byte) = 5 bytes.
    uint8_t buf[1504];// 4-byte header + 1500-byte max UDP payload
    for (;;) {
        uint32_t senderAddr = 0;
        uint16_t senderPort = 0;
        int n = _socket.RecvFrom(buf, int(sizeof(buf)), senderAddr, senderPort);
        if (n <= 0) break;
        if (n < 5) continue;// too short: no payload after roomId

        const uint32_t roomId = GetU32LE(buf);
        const uint8_t* payload = buf + 4;
        const int payloadLen = n - 4;

        auto roomIt = _rooms.find(roomId);
        if (roomIt == _rooms.end()) {
            // New room — refuse beyond the cap so a packet flood with random
            // roomIds can't grow the map without bound, and throttle how fast
            // any one source address can create rooms in the first place.
            if (static_cast<int>(_rooms.size()) >= maxRooms) continue;
            if (!_allowNewRoom(senderAddr)) continue;
            roomIt = _rooms.emplace(roomId, Room{}).first;
        }
        Room& room = roomIt->second;
        room.lastActivityMs = _totalMs;

        // Find which slot this sender occupies (or assign one).
        int senderIdx = -1;
        for (int i = 0; i < 2; i++) {
            if (room.peers[i].valid && room.peers[i].addr == senderAddr && room.peers[i].port == senderPort) {
                senderIdx = i;
                break;
            }
        }
        if (senderIdx < 0) {
            // New sender — take the first empty slot.
            for (int i = 0; i < 2; i++) {
                if (!room.peers[i].valid) {
                    senderIdx = i;
                    break;
                }
            }
        }
        if (senderIdx < 0) {
            // Room full. A mobile client that switched networks re-appears as a
            // new address while its old slot is still registered — allow the
            // newcomer to replace a peer that has gone silent, but never a
            // live one (that would let a roomId-guesser hijack the session).
            int staleIdx = -1;
            uint32_t oldestSeen = UINT32_MAX;
            for (int i = 0; i < 2; i++) {
                if (room.peers[i].lastSeenMs < oldestSeen) {
                    oldestSeen = room.peers[i].lastSeenMs;
                    staleIdx = i;
                }
            }
            if (staleIdx >= 0 && _totalMs - oldestSeen > kPeerStaleMs) {
                senderIdx = staleIdx;
            } else {
                continue;// both peers live — drop the unknown sender
            }
        }

        room.peers[senderIdx] = { senderAddr, senderPort, true, _totalMs };

        const int otherIdx = 1 - senderIdx;
        if (room.peers[otherIdx].valid) _forwardTo(room.peers[otherIdx], payload, payloadLen);
    }
}

void UdpRelay::_evictStaleRooms() {
    for (auto it = _rooms.begin(); it != _rooms.end();) {
        if (_totalMs - it->second.lastActivityMs > kRoomTimeoutMs) {
            spdlog::debug("UdpRelay: evicting idle room {}", it->first);
            it = _rooms.erase(it);
        } else {
            ++it;
        }
    }
}

bool UdpRelay::_allowNewRoom(uint32_t senderAddr) {
    IpBudget& budget = _ipBudgets[senderAddr];
    if (_totalMs - budget.windowStartMs >= rateLimitWindowMs) {
        budget.windowStartMs = _totalMs;
        budget.roomsThisWindow = 0;
    }
    if (budget.roomsThisWindow >= maxNewRoomsPerIpPerWindow) {
        spdlog::debug("UdpRelay: rate-limiting new-room request from {:#010x}", senderAddr);
        return false;
    }
    budget.roomsThisWindow++;
    return true;
}

void UdpRelay::_evictStaleIpBudgets() {
    // A budget whose window closed a full window ago hasn't tried to open a
    // room since; forgetting it is safe (a fresh attempt just starts a new
    // window) and keeps this map from growing forever with one-off senders.
    for (auto it = _ipBudgets.begin(); it != _ipBudgets.end();) {
        if (_totalMs - it->second.windowStartMs > rateLimitWindowMs) {
            it = _ipBudgets.erase(it);
        } else {
            ++it;
        }
    }
}

void UdpRelay::_forwardTo(const Peer& dst, const uint8_t* payload, int len) {
    if (!dst.valid) return;
    _socket.SendTo(dst.addr, dst.port, payload, len);
}

#endif// !__EMSCRIPTEN__

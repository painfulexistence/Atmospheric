#include "udp_relay_client.hpp"

#ifndef __EMSCRIPTEN__

#include <cstring>

namespace {
    void PutU32LE(uint8_t* p, uint32_t v) {
        p[0] = static_cast<uint8_t>(v);
        p[1] = static_cast<uint8_t>(v >> 8);
        p[2] = static_cast<uint8_t>(v >> 16);
        p[3] = static_cast<uint8_t>(v >> 24);
    }
}// namespace

bool UdpRelayClient::Connect(const std::string& relayIp, uint16_t relayPort, uint32_t roomId) {
    if (!UdpSocket::Resolve(relayIp, relayPort, _relayAddr, _relayPort)) {
        _connected = false;
        return false;
    }
    _roomId = roomId;
    _connected = true;
    return true;
}

bool UdpRelayClient::Send(UdpSocket& sock, const uint8_t* data, int len) const {
    if (!_connected || len < 0 || len > 1500) return false;

    uint8_t buf[4 + 1500];
    PutU32LE(buf, _roomId);
    std::memcpy(buf + 4, data, static_cast<size_t>(len));

    sock.SendTo(_relayAddr, _relayPort, buf, 4 + len);
    return true;
}

#endif// !__EMSCRIPTEN__

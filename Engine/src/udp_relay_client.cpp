#include "udp_relay_client.hpp"

#ifndef __EMSCRIPTEN__

#include <cstring>

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
#include <netinet/in.h>
#include <sys/socket.h>
#endif

namespace {
    void PutU32LE(uint8_t* p, uint32_t v) {
        p[0] = static_cast<uint8_t>(v);
        p[1] = static_cast<uint8_t>(v >> 8);
        p[2] = static_cast<uint8_t>(v >> 16);
        p[3] = static_cast<uint8_t>(v >> 24);
    }
}// namespace

bool UdpRelayClient::Connect(const std::string& relayIp, uint16_t relayPort, uint32_t roomId) {
    in_addr a{};
    if (inet_pton(AF_INET, relayIp.c_str(), &a) != 1) {
        _connected = false;
        return false;
    }
    _relayAddr = a.s_addr;
    _relayPort = htons(relayPort);
    _roomId = roomId;
    _connected = true;
    return true;
}

bool UdpRelayClient::Send(SocketHandle sock, const uint8_t* data, int len) const {
    if (!_connected || len < 0 || len > 1500) return false;

    uint8_t buf[4 + 1500];
    PutU32LE(buf, _roomId);
    std::memcpy(buf + 4, data, static_cast<size_t>(len));

    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_addr.s_addr = _relayAddr;
    to.sin_port = _relayPort;
    return ::sendto(
               sock, reinterpret_cast<const char*>(buf), 4 + len, 0, reinterpret_cast<const sockaddr*>(&to), sizeof(to)
           )
           >= 0;
}

#endif// !__EMSCRIPTEN__

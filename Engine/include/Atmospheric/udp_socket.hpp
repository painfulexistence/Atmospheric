#pragma once
#include <cstdint>
#include <string>

// UdpSocket
//
// Minimal non-blocking UDP socket: owns exactly one OS socket, and wraps the
// POSIX/Winsock2 platform split, ephemeral port resolution, and the
// open/bind/send/recv/close lifecycle. Carries no protocol opinion at all —
// no packet framing, no peer bookkeeping, no reliability — that is entirely
// the caller's job.
//
// Every UDP class in the engine and examples used to hand-roll a near-
// identical copy of this (LockstepNet, UdpRelay, UdpRelayClient's caller,
// HiddenTag's ClientNet and HiddenTagAuthority) before it was extracted
// here — four independent copies of the same ~30 lines of platform shim.
//
// Not available on Emscripten (no raw UDP sockets in the browser).
#ifndef __EMSCRIPTEN__

class UdpSocket {
public:
    ~UdpSocket();

    // Opens the socket and binds it. port 0 binds an OS-assigned ephemeral
    // port (BoundPort() reports it) — the usual choice for a socket that
    // only ever sends to one known target rather than accepting unsolicited
    // traffic. Safe to call again (closes any existing socket first).
    bool Open(uint16_t port = 0);
    void Close();

    void SendTo(uint32_t addr, uint16_t port, const uint8_t* data, int len);

    // Non-blocking single-datagram receive. Returns bytes received (> 0),
    // or <= 0 if nothing is pending. Call in a loop until it returns <= 0
    // to drain everything queued this frame.
    int RecvFrom(uint8_t* buf, int maxLen, uint32_t& fromAddr, uint16_t& fromPort);

    bool IsOpen() const {
        return _sock != kInvalidSocket;
    }
    uint16_t BoundPort() const {
        return _port;
    }

    // Resolves an IPv4 host string into the (address, port) form SendTo and
    // the fromAddr/fromPort pair from RecvFrom use — both network byte
    // order, so a resolved target can be compared directly against a sender
    // address without any manual htons/ntohs.
    static bool Resolve(const std::string& ip, uint16_t port, uint32_t& outAddr, uint16_t& outPort);

private:
#if defined(_WIN32)
    using SocketHandle = uintptr_t;
    static constexpr SocketHandle kInvalidSocket = SocketHandle(~uintptr_t(0));
#else
    using SocketHandle = int;
    static constexpr SocketHandle kInvalidSocket = SocketHandle(-1);
#endif

    SocketHandle _sock = kInvalidSocket;
    uint16_t _port = 0;
};

#endif// !__EMSCRIPTEN__

#include "udp_socket.hpp"

#ifndef __EMSCRIPTEN__

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

UdpSocket::~UdpSocket() {
    Close();
}

bool UdpSocket::Open(uint16_t port) {
    if (_sock != kInvalidSocket) Close();
    if (!EnsureSocketLib()) {
        spdlog::error("UdpSocket: socket subsystem init failed");
        return false;
    }
    _sock = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (_sock == kInvalidSocket) {
        spdlog::error("UdpSocket: socket() failed");
        return false;
    }
    SetNonBlocking(_sock);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (::bind(_sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        spdlog::error("UdpSocket: bind() failed on port {} (port in use?)", port);
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
    return true;
}

void UdpSocket::Close() {
    if (_sock != kInvalidSocket) {
        CloseSocketHandle(_sock);
        _sock = kInvalidSocket;
    }
    _port = 0;
}

void UdpSocket::SendTo(uint32_t addr, uint16_t port, const uint8_t* data, int len) {
    if (_sock == kInvalidSocket) return;
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_addr.s_addr = addr;
    to.sin_port = port;
    ::sendto(_sock, reinterpret_cast<const char*>(data), len, 0, reinterpret_cast<sockaddr*>(&to), sizeof(to));
}

int UdpSocket::RecvFrom(uint8_t* buf, int maxLen, uint32_t& fromAddr, uint16_t& fromPort) {
    if (_sock == kInvalidSocket) return -1;
    sockaddr_in from{};
    socklen_t fromLen = sizeof(from);
    int n = ::recvfrom(_sock, reinterpret_cast<char*>(buf), maxLen, 0, reinterpret_cast<sockaddr*>(&from), &fromLen);
    if (n > 0) {
        fromAddr = from.sin_addr.s_addr;
        fromPort = from.sin_port;
    }
    return n;
}

bool UdpSocket::Resolve(const std::string& ip, uint16_t port, uint32_t& outAddr, uint16_t& outPort) {
    in_addr a{};
    if (inet_pton(AF_INET, ip.c_str(), &a) != 1) return false;
    outAddr = a.s_addr;
    outPort = htons(port);
    return true;
}

#endif// !__EMSCRIPTEN__

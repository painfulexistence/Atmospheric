#pragma once
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

// LoopbackDatagramSocket — an in-process implementation of the DatagramSocket
// contract (see datagram_socket.hpp), moving datagrams between endpoints in the
// same process via a shared port registry, with no OS sockets at all.
//
// It exists for --local single-player on the web: the browser has no raw UDP,
// but --local is genuinely a single process (a client plus an embedded
// authority talking over loopback), so the "network" can be pure in-memory
// message passing. It faithfully emulates UDP-loopback semantics — bind a port,
// send to a port, receive with the sender's port — so the examples' existing
// --local code (bind, Resolve, SendTo by port) runs unchanged.
//
// Same method surface as UdpSocket, so DatagramSocket can alias to it on web.
// Dependency-free. Assumes a single game process (the registry is process-wide).
class LoopbackDatagramSocket {
public:
    ~LoopbackDatagramSocket();

    bool Open(uint16_t port = 0);// 0 = assign an unused ephemeral port
    void Close();

    void SendTo(uint32_t addr, uint16_t port, const uint8_t* data, int len);
    int RecvFrom(uint8_t* buf, int maxLen, uint32_t& fromAddr, uint16_t& fromPort);

    bool IsOpen() const {
        return _port != 0;
    }
    uint16_t BoundPort() const {
        return _port;
    }

    // Loopback addressing: any host maps to the loopback address; only the port
    // routes. Kept for contract parity with UdpSocket::Resolve.
    static bool Resolve(const std::string& ip, uint16_t port, uint32_t& outAddr, uint16_t& outPort);

private:
    struct Datagram {
        uint16_t srcPort;
        std::vector<uint8_t> bytes;
    };
    uint16_t _port = 0;         // 0 = closed
    std::deque<Datagram> _inbox;// datagrams delivered to this socket, awaiting RecvFrom
};

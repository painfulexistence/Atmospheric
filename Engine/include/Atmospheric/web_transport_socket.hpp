#pragma once
#ifdef __EMSCRIPTEN__
#include <cstdint>
#include <string>

// WebTransportSocket — a DatagramSocket-shaped wrapper over a browser
// WebTransport session (QUIC datagram mode), the planned transport for browser
// client <-> dedicated server PvP (the browser has no raw UDP).
//
// STATUS: WIP skeleton. The EM_JS glue and the C++ surface exist, but it is not
// yet wired into DatagramSocket or ClientNet, and has not been verified against
// a real WebTransport server. Web --local still uses LoopbackDatagramSocket.
//
// WebTransport is connection-ORIENTED and async, unlike UDP's connectionless
// SendTo(addr,port). A client only ever talks to one server, so the real entry
// point is Connect(url) + IsOpen(); Send/Recv move datagrams over that single
// session. The SendTo/RecvFrom/Open/Resolve members are thin shims so the type
// still satisfies the DatagramSocket contract the netcode is written against —
// but the actual ClientNet wiring will call Connect(url) on web rather than the
// Open + Resolve + SendTo(hello) dance, because there is no local bind and the
// session must be ready before the first datagram lands.
//
// TLS: with a CA-signed cert for your domain (e.g. Let's Encrypt) the browser
// trusts it and Connect("https://your.domain:port/path") just works. A
// self-signed cert would instead need `serverCertificateHashes` passed to the
// WebTransport constructor (see the .cpp).
class WebTransportSocket {
public:
    ~WebTransportSocket();

    // Start the async WebTransport session to an https URL. Returns false only
    // on an obviously bad argument; success/failure of the handshake surfaces
    // later via IsOpen()/IsConnecting()/Failed().
    bool Connect(const std::string& url);
    void Close();

    bool IsOpen() const;// session established (wt.ready resolved)
    bool IsConnecting() const;// handshake in flight
    bool Failed() const;// handshake failed or session closed

    void Send(const uint8_t* data, int len);// write one datagram
    int Recv(uint8_t* buf, int maxLen);// pop one queued datagram, or 0

    // ── DatagramSocket contract shims (single session; addr/port ignored) ──
    bool Open(uint16_t /*port*/ = 0) {
        return true;// no local bind with WebTransport; Connect(url) does the work
    }
    void SendTo(uint32_t /*addr*/, uint16_t /*port*/, const uint8_t* data, int len) {
        Send(data, len);
    }
    int RecvFrom(uint8_t* buf, int maxLen, uint32_t& fromAddr, uint16_t& fromPort) {
        fromAddr = 0;
        fromPort = 0;
        return Recv(buf, maxLen);
    }
    uint16_t BoundPort() const {
        return 0;
    }
    static bool Resolve(const std::string& /*ip*/, uint16_t port, uint32_t& outAddr, uint16_t& outPort) {
        outAddr = 0;
        outPort = port;
        return true;
    }
};

#endif// __EMSCRIPTEN__

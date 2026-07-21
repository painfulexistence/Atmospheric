#pragma once
#include "datagram_socket.hpp"
#include "net_conditioner.hpp"// also pulls in net_metrics.hpp + PumpConditioned
#include <cstdint>
#include <utility>

// NetEndpoint — "stand up a client or server in three lines." Bundles the three
// things every realtime endpoint re-assembles by hand — the datagram transport,
// the link emulator, and the metrics — behind Open / Send / Poll, so per-game
// net code stops re-owning them and re-writing the conditioned receive loop:
//
//   NetEndpoint net;
//   net.Open(0);
//   net.Send(addr, port, bytes, len);                       // counted in Metrics()
//   net.Poll(nowMs, [&](const uint8_t* p, int n, uint32_t a, uint16_t port) {
//       ... handle one datagram ...
//   });
//   net.Metrics();  net.Conditioner();                      // netgraph + dial the link
//
// Byte counting and the conditioner routing live here once (via PumpConditioned)
// instead of in every ClientNet. Templated on the socket so it works over any
// DatagramSocket-shaped transport — real UDP, in-process loopback, WebTransport —
// and is unit-testable off a loopback socket without real networking.
template <class Socket = DatagramSocket>
class NetEndpoint {
public:
    bool Open(uint16_t port = 0) {
        return _socket.Open(port);
    }
    void Close() {
        _socket.Close();
    }
    bool IsOpen() const {
        return _socket.IsOpen();
    }
    uint16_t BoundPort() const {
        return _socket.BoundPort();
    }

    void Send(uint32_t addr, uint16_t port, const uint8_t* data, int len) {
        _socket.SendTo(addr, port, data, len);
        _metrics.OnSent(len);
    }

    // Drain this frame's inbound datagrams through the conditioner + metrics,
    // dispatching each ready one to handle(data, len, fromAddr, fromPort).
    template <class Handle>
    void Poll(uint32_t nowMs, Handle&& handle) {
        PumpConditioned(
            _cond,
            _metrics,
            nowMs,
            [&](uint8_t* b, int max, uint32_t& from, uint16_t& port) { return _socket.RecvFrom(b, max, from, port); },
            std::forward<Handle>(handle)
        );
    }

    NetMetrics& Metrics() {
        return _metrics;
    }
    const NetMetrics& Metrics() const {
        return _metrics;
    }
    NetConditioner& Conditioner() {
        return _cond;
    }

    // Escape hatch for the rare caller that needs the raw transport (e.g. passing
    // it to a relay client). Prefer Open/Send/Poll.
    Socket& Transport() {
        return _socket;
    }

private:
    Socket _socket;
    NetConditioner _cond;
    NetMetrics _metrics;
};

#pragma once
#include "net_metrics.hpp"
#include <cstdint>
#include <vector>

// NetConditioner — inbound-datagram link emulator: latency, jitter, loss, and
// duplication. Lets realtime netcode (prediction, reconciliation, interpolation,
// lag compensation) run under adverse conditions instead of only on a clean
// loopback. Dependency-free so the headless servers can use it too, and
// deterministic for a fixed seed so tests can assert behaviour.
//
// Inbound path: feed every real arrival to Push(), then drain Pop() for the
// datagrams whose delivery time has come. With all knobs at 0 it is a
// pass-through (Pop returns each pushed datagram immediately, in order), so
// callers gate on Active() to skip the queue on a clean link — or use the
// PumpConditioned() helper below, which does that for them.
class NetConditioner {
public:
    // Tunable live; safe to change any frame.
    int latencyMs = 0;// base one-way delay added to every delivered packet
    int jitterMs = 0;// uniform +/- added to latency (reordering falls out of this)
    float lossPct = 0.0f;// 0..100: chance to drop a packet outright
    float dupPct = 0.0f;// 0..100: chance to also deliver a second copy

    explicit NetConditioner(uint32_t seed = 0x9E3779B9u) : _rng(seed ? seed : 1u) {
    }

    bool Active() const {
        return latencyMs != 0 || jitterMs != 0 || lossPct > 0.0f || dupPct > 0.0f;
    }

    // Ingest one real arrival: apply loss, else schedule delivery at
    // now + latency +/- jitter (and maybe a duplicate).
    void Push(uint32_t nowMs, uint32_t fromAddr, uint16_t fromPort, const uint8_t* data, int len);

    // Emit the earliest datagram whose delivery time has arrived, or false when
    // none is due. Call in a loop each frame.
    bool Pop(uint32_t nowMs, uint32_t& fromAddr, uint16_t& fromPort, uint8_t* buf, int maxLen, int& outLen);

    // Actually realized drop rate (EWMA over recent packets), to show beside the
    // dialed lossPct knob. Decays as the knob changes.
    float MeasuredLossPct() const {
        return _lossEwma * 100.0f;
    }

private:
    struct Held {
        uint32_t dueMs;
        uint32_t fromAddr;
        uint16_t fromPort;
        std::vector<uint8_t> bytes;
    };
    std::vector<Held> _q;
    uint32_t _rng;
    float _lossEwma = 0.0f;

    uint32_t NextU32();// xorshift32
    float NextUnit();// [0,1)
    int NextJitter();// [-jitterMs, +jitterMs]
    void Enqueue(uint32_t nowMs, uint32_t addr, uint16_t port, const uint8_t* data, int len);
};

// PumpConditioned — one frame of inbound conditioning, shared by every realtime
// client. Pulls raw datagrams via recv(), counts + routes them through the
// conditioner, and dispatches the ready ones to handle(). Templated on the
// receive source so it works over both a UdpSocket and Emscripten's WebRTC glue.
//   recv(buf, maxLen, fromAddr, fromPort) -> bytes (>0), or <=0 when drained
//   handle(buf, len, fromAddr, fromPort)
template<class Recv, class Handle>
void PumpConditioned(NetConditioner& cond, NetMetrics& metrics, uint32_t nowMs, Recv&& recv, Handle&& handle) {
    metrics.Roll(nowMs);
    uint8_t buf[1500];
    uint32_t from = 0;
    uint16_t port = 0;
    int n = 0;
    while ((n = recv(buf, static_cast<int>(sizeof buf), from, port)) > 0) {
        metrics.OnRecv(n);// count on the wire (pre-conditioning) for true link bandwidth
        if (cond.Active())
            cond.Push(nowMs, from, port, buf, n);
        else
            handle(buf, n, from, port);
    }
    if (cond.Active())
        while (cond.Pop(nowMs, from, port, buf, static_cast<int>(sizeof buf), n))
            handle(buf, n, from, port);
}

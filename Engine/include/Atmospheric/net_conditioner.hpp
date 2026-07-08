#pragma once
#include <cstdint>
#include <vector>

// NetConditioner — inbound-datagram link emulator: latency, jitter, loss, and
// duplication. Lets the realtime netcode above it (prediction, reconciliation,
// interpolation, lag compensation) actually be exercised under the adverse
// conditions it exists for, instead of only ever running on a ~0 ms / 0 %-loss
// loopback where none of that code is ever stressed.
//
// Dependency-free (no engine or third-party headers) so the headless dep-free
// servers (HideAndSeekServer, RelayServer) can use it too, not just windowed
// clients — the same reason UdpSocket has no engine deps. Deterministic for a
// fixed seed, so tests can assert exact behaviour.
//
// Inbound path — feed every real arrival into Push(), then drain Pop() for the
// datagrams whose scheduled delivery time has arrived:
//
//   int n; uint32_t from; uint16_t port;
//   while ((n = sock.RecvFrom(buf, sizeof buf, from, port)) > 0)
//       cond.Push(nowMs, from, port, buf, n);
//   while (cond.Pop(nowMs, from, port, buf, sizeof buf, n)) {
//       /* handle the n-byte datagram from (from,port) */
//   }
//
// With all knobs at 0 it is a pass-through: Pop returns each pushed datagram
// immediately and in order (so callers can gate on Active() and skip the queue
// entirely on a clean loopback — see the example ClientNet integrations).
class NetConditioner {
public:
    // Tunable live; safe to change any frame.
    int latencyMs = 0;   // base one-way delay added to every delivered packet
    int jitterMs = 0;    // uniform +/- added to latency (reordering emerges from this, as in reality)
    float lossPct = 0.0f;// 0..100: chance to drop a packet outright
    float dupPct = 0.0f; // 0..100: chance to also deliver a second copy

    explicit NetConditioner(uint32_t seed = 0x9E3779B9u) : _rng(seed ? seed : 1u) {}

    bool Active() const {
        return latencyMs != 0 || jitterMs != 0 || lossPct > 0.0f || dupPct > 0.0f;
    }

    // Ingest one real arrival. Applies loss (may drop), then schedules delivery
    // at now + latency +/- jitter, and may schedule a duplicate.
    void Push(uint32_t nowMs, uint32_t fromAddr, uint16_t fromPort, const uint8_t* data, int len);

    // Emit the earliest datagram whose delivery time has arrived. Returns false
    // when nothing is due. Call in a loop each frame.
    bool Pop(uint32_t nowMs, uint32_t& fromAddr, uint16_t& fromPort, uint8_t* buf, int maxLen, int& outLen);

    // Rolling drop rate over packets seen since the last reset — the honest
    // number to show next to a dialed loss knob ("dialed 10 %, link shows ~10 %").
    void ResetWindow() {
        _seen = 0;
        _dropped = 0;
    }
    float MeasuredLossPct() const {
        return _seen ? 100.0f * static_cast<float>(_dropped) / static_cast<float>(_seen) : 0.0f;
    }
    int InFlight() const {
        return static_cast<int>(_q.size());
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
    uint32_t _seen = 0;
    uint32_t _dropped = 0;

    uint32_t NextU32();  // xorshift32
    float NextUnit();    // [0,1)
    int NextJitter();    // [-jitterMs, +jitterMs]
    void Enqueue(uint32_t nowMs, uint32_t addr, uint16_t port, const uint8_t* data, int len);
};

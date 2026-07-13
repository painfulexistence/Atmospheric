#pragma once
#include <cstdint>

// NetMetrics — counters + derived rates for one realtime UDP endpoint, sized
// for an on-screen netgraph. Split of responsibility:
//   - the transport feeds raw byte/packet counts as datagrams cross the wire
//     (OnSent / OnRecv);
//   - the netcode layer feeds the signals only it can derive — RTT from
//     input/ack latency, prediction error at reconciliation, pending-input
//     depth — because those aren't visible at the socket.
// Call Roll(nowMs) once per frame to turn the last second of bytes into kbit/s.
//
// Dependency-free, header-only: usable by the dep-free servers as well as
// windowed clients (a server can expose the same numbers over a status port).
struct NetMetrics {
    uint64_t packetsOut = 0, packetsIn = 0;
    uint64_t bytesOut = 0, bytesIn = 0;
    float kbpsOut = 0.0f, kbpsIn = 0.0f;// over the last ~1 s window

    float rttMs = 0.0f;// EWMA of measured round trips (0 until first sample)
    float lossPct = 0.0f;// inbound loss estimate, 0..100
    float predErr = -1.0f;// prediction correction at last reconcile, in the model's own
                          // distance units (metres, world units, ...); <0 = n/a for this model
    int pendingInputs = -1;// unacked predicted inputs in flight; <0 = n/a for this model

    void OnSent(int len) {
        packetsOut++;
        bytesOut += static_cast<uint32_t>(len);
        _winOut += static_cast<uint32_t>(len);
    }
    void OnRecv(int len) {
        packetsIn++;
        bytesIn += static_cast<uint32_t>(len);
        _winIn += static_cast<uint32_t>(len);
    }
    void FeedRtt(float sampleMs) {
        rttMs = _haveRtt ? rttMs * 0.9f + sampleMs * 0.1f : sampleMs;// EWMA, seeded by first sample
        _haveRtt = true;
    }

    void Roll(uint32_t nowMs) {
        if (!_started) {
            _started = true;
            _winMs = nowMs;
            return;
        }
        const uint32_t dt = nowMs - _winMs;
        if (dt >= 1000) {
            kbpsOut = static_cast<float>(_winOut) * 8.0f / static_cast<float>(dt);// bits over dt ms = kbit/s
            kbpsIn = static_cast<float>(_winIn) * 8.0f / static_cast<float>(dt);
            _winOut = _winIn = 0;
            _winMs = nowMs;
        }
    }

private:
    uint32_t _winOut = 0, _winIn = 0, _winMs = 0;
    bool _started = false, _haveRtt = false;
};

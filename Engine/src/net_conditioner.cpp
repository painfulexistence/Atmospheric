#include "net_conditioner.hpp"

uint32_t NetConditioner::NextU32() {
    // xorshift32 — tiny, fast, dependency-free, and deterministic per seed.
    uint32_t x = _rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    _rng = x;
    return x;
}

float NetConditioner::NextUnit() {
    return static_cast<float>(NextU32() >> 8) / static_cast<float>(1u << 24);// 24-bit → [0,1)
}

int NetConditioner::NextJitter() {
    if (jitterMs <= 0) return 0;
    const uint32_t span = static_cast<uint32_t>(2 * jitterMs + 1);
    return static_cast<int>(NextU32() % span) - jitterMs;// uniform in [-jitterMs, +jitterMs]
}

void NetConditioner::Enqueue(uint32_t nowMs, uint32_t addr, uint16_t port, const uint8_t* data, int len) {
    int delay = latencyMs + NextJitter();
    if (delay < 0) delay = 0;
    Held h;
    h.dueMs = nowMs + static_cast<uint32_t>(delay);
    h.fromAddr = addr;
    h.fromPort = port;
    h.bytes.assign(data, data + len);
    _q.push_back(std::move(h));
}

void NetConditioner::Push(uint32_t nowMs, uint32_t fromAddr, uint16_t fromPort, const uint8_t* data, int len) {
    if (len <= 0) return;
    _seen++;
    if (lossPct > 0.0f && NextUnit() * 100.0f < lossPct) {
        _dropped++;
        return;
    }
    Enqueue(nowMs, fromAddr, fromPort, data, len);
    if (dupPct > 0.0f && NextUnit() * 100.0f < dupPct)
        Enqueue(nowMs, fromAddr, fromPort, data, len);
}

bool NetConditioner::Pop(uint32_t nowMs, uint32_t& fromAddr, uint16_t& fromPort, uint8_t* buf, int maxLen, int& outLen) {
    // Pick the earliest-due packet that is due now. Serial-number comparison
    // (int32_t of the difference) so it stays correct across uint32 ms wrap and
    // preserves arrival order when jitter == 0.
    int best = -1;
    for (int i = 0; i < static_cast<int>(_q.size()); i++) {
        if (static_cast<int32_t>(_q[i].dueMs - nowMs) <= 0) {// due (not in the future)
            if (best < 0 || static_cast<int32_t>(_q[i].dueMs - _q[best].dueMs) < 0) best = i;
        }
    }
    if (best < 0) return false;

    Held& h = _q[best];
    int n = static_cast<int>(h.bytes.size());
    if (n > maxLen) n = maxLen;
    for (int i = 0; i < n; i++) buf[i] = h.bytes[i];
    outLen = n;
    fromAddr = h.fromAddr;
    fromPort = h.fromPort;
    _q.erase(_q.begin() + best);
    return true;
}

#include "loopback_datagram_socket.hpp"

#include <mutex>
#include <unordered_map>

namespace {
    // Process-wide registry of bound loopback sockets, keyed by port. The mutex
    // guards it in case the engine's worker threads ever touch the net path;
    // --local traffic is single-threaded, so contention is nil.
    std::mutex g_mutex;
    std::unordered_map<uint16_t, LoopbackDatagramSocket*> g_bound;
    uint16_t g_nextEphemeral = 40000;// climbs; wraps back into the ephemeral range

    constexpr uint32_t kLoopbackAddr = 0x7F000001u;// 127.0.0.1, for RecvFrom's fromAddr
}// namespace

LoopbackDatagramSocket::~LoopbackDatagramSocket() {
    Close();
}

bool LoopbackDatagramSocket::Open(uint16_t port) {
    Close();
    std::lock_guard<std::mutex> lock(g_mutex);
    if (port == 0) {
        // Find an unused ephemeral port.
        for (int tries = 0; tries < 65535; tries++) {
            if (g_nextEphemeral < 40000) g_nextEphemeral = 40000;
            uint16_t candidate = g_nextEphemeral++;
            if (g_bound.find(candidate) == g_bound.end()) {
                port = candidate;
                break;
            }
        }
        if (port == 0) return false;// registry full (not realistically reachable)
    } else if (g_bound.find(port) != g_bound.end()) {
        return false;// port already bound, like a real bind conflict
    }
    _port = port;
    g_bound[_port] = this;
    return true;
}

void LoopbackDatagramSocket::Close() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (_port != 0) {
        auto it = g_bound.find(_port);
        if (it != g_bound.end() && it->second == this) g_bound.erase(it);
        _port = 0;
    }
    _inbox.clear();
}

void LoopbackDatagramSocket::SendTo(uint32_t /*addr*/, uint16_t port, const uint8_t* data, int len) {
    if (_port == 0 || len <= 0) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_bound.find(port);// route by destination port only (loopback)
    if (it == g_bound.end()) return;// nothing bound there — datagram dropped, like UDP
    it->second->_inbox.push_back(Datagram{ _port, std::vector<uint8_t>(data, data + len) });
}

int LoopbackDatagramSocket::RecvFrom(uint8_t* buf, int maxLen, uint32_t& fromAddr, uint16_t& fromPort) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (_port == 0 || _inbox.empty()) return 0;
    Datagram dg = std::move(_inbox.front());
    _inbox.pop_front();
    int n = static_cast<int>(dg.bytes.size());
    if (n > maxLen) n = maxLen;
    for (int i = 0; i < n; i++)
        buf[i] = dg.bytes[i];
    fromAddr = kLoopbackAddr;
    fromPort = dg.srcPort;
    return n;
}

bool LoopbackDatagramSocket::Resolve(const std::string& /*ip*/, uint16_t port, uint32_t& outAddr, uint16_t& outPort) {
    outAddr = kLoopbackAddr;
    outPort = port;
    return true;
}

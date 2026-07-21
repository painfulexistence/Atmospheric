// Unit tests for NetConditioner: pass-through when idle, latency scheduling,
// total loss, and determinism for a fixed seed (the property the reliability and
// reconciliation tests rely on to be reproducible).
#include "net_conditioner.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace {
    void Push(NetConditioner& c, uint32_t nowMs, const std::string& s) {
        c.Push(nowMs, 1, 1, reinterpret_cast<const uint8_t*>(s.data()), static_cast<int>(s.size()));
    }
    // Drain everything due at nowMs into a list of payloads.
    std::vector<std::string> PopAll(NetConditioner& c, uint32_t nowMs) {
        std::vector<std::string> out;
        uint8_t buf[256];
        uint32_t addr;
        uint16_t port;
        int n;
        while (c.Pop(nowMs, addr, port, buf, sizeof buf, n))
            out.emplace_back(reinterpret_cast<char*>(buf), static_cast<size_t>(n));
        return out;
    }
}// namespace

TEST_CASE("idle conditioner is a pass-through, in order", "[conditioner]") {
    NetConditioner c;// all knobs 0
    CHECK_FALSE(c.Active());
    Push(c, 0, "a");
    Push(c, 0, "b");
    Push(c, 0, "c");
    CHECK(PopAll(c, 0) == std::vector<std::string>{"a", "b", "c"});
}

TEST_CASE("latency delays delivery until its due time", "[conditioner]") {
    NetConditioner c;
    c.latencyMs = 50;// no jitter -> exact
    CHECK(c.Active());
    Push(c, 100, "hi");
    CHECK(PopAll(c, 100).empty());        // not due yet
    CHECK(PopAll(c, 149).empty());        // still early
    CHECK(PopAll(c, 150) == std::vector<std::string>{"hi"});// due at push + latency
}

TEST_CASE("100% loss drops everything", "[conditioner]") {
    NetConditioner c;
    c.lossPct = 100.0f;
    for (int i = 0; i < 50; i++) Push(c, 0, "x");
    CHECK(PopAll(c, 1000).empty());
}

TEST_CASE("same seed produces the same drop pattern (determinism)", "[conditioner]") {
    auto run = [] {
        NetConditioner c(12345);// fixed seed
        c.lossPct = 40.0f;
        std::vector<std::string> got;
        for (int i = 0; i < 200; i++) {
            Push(c, 0, "m" + std::to_string(i));
            auto batch = PopAll(c, 0);// latency 0 -> due immediately, dups included
            for (auto& s : batch) got.push_back(s);
        }
        return got;
    };
    CHECK(run() == run());// bit-for-bit reproducible
}

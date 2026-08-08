// Unit tests for ReliableChannel: exactly-once, in-order delivery per channel
// over a lossy/reordering/duplicating link; independent channels (no
// cross-channel head-of-line blocking); and bounded-window backpressure.
#include "net_conditioner.hpp"
#include "reliable_channel.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace {
    // Pump one endpoint's outbound packet through a conditioner into the other.
    void Pump(ReliableChannel& from, NetConditioner& link, ReliableChannel& to, uint32_t nowMs) {
        uint8_t pkt[1200];
        int n = from.WritePacket(pkt, sizeof pkt);
        if (n > 0) link.Push(nowMs, 1, 1, pkt, n);
        uint32_t a;
        uint16_t p;
        int got;
        while (link.Pop(nowMs, a, p, pkt, sizeof pkt, got)) to.ReadPacket(pkt, got);
    }

    // Little-endian wire encoders matching reliable_channel.cpp, for hand-built
    // packets in the deterministic isolation test.
    void PutU16(std::vector<uint8_t>& v, uint16_t x) {
        v.push_back(static_cast<uint8_t>(x));
        v.push_back(static_cast<uint8_t>(x >> 8));
    }
    void PutU32(std::vector<uint8_t>& v, uint32_t x) {
        for (int i = 0; i < 4; i++) v.push_back(static_cast<uint8_t>(x >> (8 * i)));
    }
    struct WireMsg {
        uint8_t channel;
        uint16_t id;
        std::string payload;
    };
    std::vector<uint8_t> BuildPacket(uint16_t seq, const std::vector<WireMsg>& msgs) {
        std::vector<uint8_t> p;
        PutU16(p, seq);
        PutU16(p, 0);// ack (irrelevant for a receive-only assertion)
        PutU32(p, 0);// ackBits
        p.push_back(static_cast<uint8_t>(msgs.size()));
        for (const WireMsg& m : msgs) {
            p.push_back(0x01);// flags: reliable
            p.push_back(m.channel);
            PutU16(p, m.id);
            PutU16(p, static_cast<uint16_t>(m.payload.size()));
            p.insert(p.end(), m.payload.begin(), m.payload.end());
        }
        return p;
    }

    std::vector<std::string> DrainReliable(ReliableChannel& ch, std::vector<int>* channels = nullptr) {
        std::vector<std::string> out;
        uint8_t buf[256];
        bool reliable;
        uint8_t c;
        int n;
        while ((n = ch.Receive(buf, sizeof buf, &reliable, &c)) > 0) {
            if (reliable) {
                out.emplace_back(reinterpret_cast<char*>(buf), static_cast<size_t>(n));
                if (channels) channels->push_back(c);
            }
        }
        return out;
    }

    bool SendStr(ReliableChannel& ch, const std::string& s, uint8_t channel) {
        return ch.SendReliable(reinterpret_cast<const uint8_t*>(s.data()), static_cast<int>(s.size()), channel);
    }
}// namespace

TEST_CASE("reliable messages arrive exactly once, in order per channel, under heavy loss", "[reliable]") {
    ReliableChannel A, B;
    NetConditioner aToB(1), bToA(2);// independent seeds -> deterministic
    aToB.lossPct = 30.0f;
    aToB.jitterMs = 40;// jitter => reordering
    aToB.dupPct = 15.0f;
    aToB.latencyMs = 20;
    bToA.lossPct = 30.0f;// ack packets are lossy too
    bToA.jitterMs = 40;
    bToA.latencyMs = 20;

    const int kCh = ReliableChannel::kNumChannels;
    const int perCh = 150;
    const int N = perCh * kCh;
    int nextToQueue = 0;// global index; channel = idx % kCh, per-channel seq = idx / kCh

    for (int i = 0; i < 20; i++) {// a few unreliable, best-effort
        std::string s = "unrel-" + std::to_string(i);
        A.SendUnreliable(reinterpret_cast<const uint8_t*>(s.data()), static_cast<int>(s.size()));
    }

    std::vector<std::vector<std::string>> delivered(static_cast<size_t>(kCh));
    int totalReliable = 0;
    int unrelDelivered = 0;
    uint32_t nowMs = 0;
    for (int tick = 0; tick < 20000; tick++) {
        nowMs += 16;// ~60 Hz
        while (nextToQueue < N) {// feed the shared window until full or drained
            const uint8_t ch = static_cast<uint8_t>(nextToQueue % kCh);
            const int seq = nextToQueue / kCh;
            std::string s = "c" + std::to_string(ch) + "-" + std::to_string(seq);
            if (!SendStr(A, s, ch)) break;// window full — retry next tick
            nextToQueue++;
        }
        Pump(A, aToB, B, nowMs);// data
        Pump(B, bToA, A, nowMs);// acks

        uint8_t buf[256];
        bool reliable;
        uint8_t ch;
        int n;
        while ((n = B.Receive(buf, sizeof buf, &reliable, &ch)) > 0) {
            if (reliable) {
                delivered[ch].emplace_back(reinterpret_cast<char*>(buf), static_cast<size_t>(n));
                totalReliable++;
            } else {
                unrelDelivered++;
            }
        }
        if (totalReliable == N && A.UnackedCount() == 0) break;
    }

    CHECK(totalReliable == N);
    CHECK(A.UnackedCount() == 0);// sender's unacked queue drains
    for (int ch = 0; ch < kCh; ch++) {
        INFO("channel " << ch);
        REQUIRE(static_cast<int>(delivered[static_cast<size_t>(ch)].size()) == perCh);
        for (int i = 0; i < perCh; i++) {
            CHECK(delivered[static_cast<size_t>(ch)][static_cast<size_t>(i)] == ("c" + std::to_string(ch) + "-" + std::to_string(i)));
        }
    }
    // Unreliable is best-effort: some arrive, none are resent (so never more than sent).
    CHECK(unrelDelivered > 0);
    CHECK(unrelDelivered <= 20);
}

TEST_CASE("channels are independent: a gap on one does not block another", "[reliable][channels]") {
    ReliableChannel B;
    // Channel 0 has a gap (id 0 missing, ids 1,2 present); channel 1 is contiguous.
    auto p = BuildPacket(0,
                         {{0, 1, "c0-1"},
                          {0, 2, "c0-2"},
                          {1, 0, "c1-0"},
                          {1, 1, "c1-1"},
                          {1, 2, "c1-2"}});
    B.ReadPacket(p.data(), static_cast<int>(p.size()));

    // Channel 1 flows through despite channel 0 being blocked on its missing id 0.
    CHECK(DrainReliable(B) == std::vector<std::string>{"c1-0", "c1-1", "c1-2"});

    // Once channel 0's id 0 arrives, its buffered 1,2 flush in order.
    auto heal = BuildPacket(1, {{0, 0, "c0-0"}});
    B.ReadPacket(heal.data(), static_cast<int>(heal.size()));
    CHECK(DrainReliable(B) == std::vector<std::string>{"c0-0", "c0-1", "c0-2"});
}

TEST_CASE("SendReliable applies backpressure when the in-flight window is full", "[reliable][backpressure]") {
    ReliableChannel A;// never pumped, so nothing is ever acked
    const std::string msg = "x";
    int accepted = 0;
    for (int i = 0; i < 10000; i++)
        if (SendStr(A, msg, 0)) accepted++;
        else break;
    // Accepts up to the window, then rejects — it never grows unbounded.
    CHECK(accepted > 0);
    CHECK(accepted == A.UnackedCount());
    CHECK_FALSE(SendStr(A, msg, 0));// still full
}

TEST_CASE("SendReliable rejects an out-of-range channel", "[reliable][channels]") {
    ReliableChannel A;
    const std::string msg = "x";
    CHECK(SendStr(A, msg, 0));
    CHECK_FALSE(SendStr(A, msg, static_cast<uint8_t>(ReliableChannel::kNumChannels)));
    CHECK_FALSE(SendStr(A, msg, 200));
}

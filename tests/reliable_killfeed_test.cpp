// Integration test for the Deathmatch reliable kill-feed: the exact codec the
// game uses (proto::EncodeKill) carried over ReliableChannel through a hostile
// NetConditioner link must arrive exactly once, in order — the property that
// makes an edge-triggered event (a frag) safe to send unreliably-underneath.
//
// This is the "used, not just defined" proof for the per-channel reliability
// layer: it runs the *application* message, not a synthetic payload.
#include "net_conditioner.hpp"
#include "protocol.hpp"// Deathmatch kill-event codec (dependency-free)
#include "reliable_channel.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <utility>
#include <vector>

namespace {
    // Move one endpoint's outbound packet through a conditioner into the other.
    void Pump(ReliableChannel& from, NetConditioner& link, ReliableChannel& to, uint32_t nowMs) {
        uint8_t pkt[600];
        int n = from.WritePacket(pkt, sizeof pkt);
        if (n > 0) link.Push(nowMs, 1, 1, pkt, n);
        uint32_t a;
        uint16_t p;
        int got;
        while (link.Pop(nowMs, a, p, pkt, sizeof pkt, got)) to.ReadPacket(pkt, got);
    }
}// namespace

TEST_CASE("reliable kill-feed events survive heavy loss, exactly once and in order", "[reliable][killfeed]") {
    ReliableChannel server, client;
    NetConditioner down(1), up(2);// server→client (events) and client→server (acks)
    down.lossPct = 35.0f;
    down.jitterMs = 40.0f;// reordering
    down.dupPct = 15.0f;
    down.latencyMs = 20;
    up.lossPct = 35.0f;// acks are lossy too
    up.jitterMs = 40.0f;
    up.latencyMs = 20;

    // Queue N frags with alternating (killer, victim) so ordering is checkable.
    const int N = 100;
    for (int i = 0; i < N; i++) {
        uint8_t msg[proto::kKillMsgLen];
        proto::EncodeKill(msg, i % 2, 1 - (i % 2));
        REQUIRE(server.SendReliable(msg, proto::kKillMsgLen));
    }

    std::vector<std::pair<int, int>> got;
    uint32_t nowMs = 0;
    // Run until every event is delivered AND every ack has flowed back (so the
    // sender's resend queue drains) — not just until the client has them all,
    // or the last few lossy acks would still be in flight at loop exit.
    for (int tick = 0; tick < 5000; tick++) {
        nowMs += 16;// ~60 Hz
        Pump(server, down, client, nowMs);// events down
        Pump(client, up, server, nowMs);  // acks up

        uint8_t buf[64];
        bool reliable = false;
        int n = 0;
        while ((n = client.Receive(buf, static_cast<int>(sizeof buf), &reliable)) > 0) {
            if (reliable && n >= proto::kKillMsgLen && buf[0] == static_cast<uint8_t>(proto::RelMsg::Kill))
                got.emplace_back(static_cast<int>(buf[1]), static_cast<int>(buf[2]));
        }
        if (static_cast<int>(got.size()) == N && server.UnackedCount() == 0) break;
    }

    REQUIRE(got.size() == static_cast<size_t>(N));// none lost, none duplicated
    for (int i = 0; i < N; i++) {
        INFO("kill-feed index " << i);
        CHECK(got[static_cast<size_t>(i)].first == i % 2);      // in order
        CHECK(got[static_cast<size_t>(i)].second == 1 - (i % 2));
    }
    CHECK(server.UnackedCount() == 0);// every event acked, resend queue drained
}

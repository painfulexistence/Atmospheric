// Loopback integration test: one UdpRelayServer and two LockstepNet peers in a
// single process, talking over 127.0.0.1. No real network required, so this
// runs in CI. Verifies:
//   1. relay handshake — both peers reach Running through the relay
//   2. WELCOME propagation — client inherits the host's seed and input delay
//   3. bidirectional input exchange through the relay
//   4. stale-peer rebind — a reconnecting client (new socket/port) takes over
//      the slot of a peer that has gone silent past kPeerStaleMs
// Exit code 0 = pass; prints the failing check otherwise.
#include "net_lockstep.hpp"
#include <Atmospheric/udp_relay_server.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>

#define REQUIRE(cond)                                                   \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            return 1;                                                   \
        }                                                               \
    } while (0)

namespace {

    constexpr uint32_t kRoomId = 42;
    constexpr uint32_t kSeed = 7;
    constexpr int kDelay = 2;
    constexpr int kTicks = 50;

    uint32_t g_nowMs = 1000;// synthetic clock for LockstepNet (starts past the hello throttle)

    // One scheduler step: relay processes, peers pump, clocks advance 5 ms.
    void step(UdpRelayServer& relay, LockstepNet* a, LockstepNet* b) {
        relay.Process(0.005f);
        if (a) a->Pump(g_nowMs);
        if (b) b->Pump(g_nowMs);
        g_nowMs += 5;
        // Give loopback datagrams a moment to land in the receive buffers.
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    bool bothRunning(const LockstepNet& a, const LockstepNet& b) {
        return a.state == LockstepNet::State::Running && b.state == LockstepNet::State::Running;
    }

}// namespace

int main() {
    UdpRelayServer relay;
    REQUIRE(relay.Start(0));// ephemeral port — no CI port conflicts
    const uint16_t port = relay.BoundPort();
    REQUIRE(port != 0);
    std::printf("relay listening on 127.0.0.1:%u\n", port);

    // ── 1+2: handshake through the relay ────────────────────────────────────
    LockstepNet host, client;
    REQUIRE(host.StartRelayHost("127.0.0.1", port, kRoomId, kSeed, kDelay));
    REQUIRE(client.StartRelayClient("127.0.0.1", port, kRoomId));

    for (int i = 0; i < 2000 && !bothRunning(host, client); i++)
        step(relay, &host, &client);
    REQUIRE(bothRunning(host, client));
    REQUIRE(client.seed == kSeed);
    REQUIRE(client.inputDelay == kDelay);
    REQUIRE(relay.RoomCount() == 1);
    std::printf("handshake ok (seed/delay propagated)\n");

    // ── 3: bidirectional input exchange ─────────────────────────────────────
    // Submit one tick per step, like a real fixed-rate simulation would.
    // Real games submit for tick = simTick + inputDelay, so the first
    // submitted tick is kDelay: ticks below inputDelay are neutral by
    // protocol design (and tick 0 is never on the wire — SendInputs starts
    // at peerAckedTick + 1).
    const uint32_t firstTick = kDelay;
    const uint32_t lastTick = kDelay + kTicks - 1;
    for (uint32_t t = firstTick; t <= lastTick; t++) {
        InputFrame hf, cf;
        hf.buttons = uint8_t(t + 1);
        cf.buttons = uint8_t(t + 101);
        host.SubmitLocalInput(t, hf);
        client.SubmitLocalInput(t, cf);
        step(relay, &host, &client);
    }
    for (int i = 0; i < 2000; i++) {
        if (host.HasInputs(lastTick) && client.HasInputs(lastTick)) break;
        step(relay, &host, &client);
    }
    for (uint32_t t = firstTick; t <= lastTick; t++) {
        REQUIRE(host.HasInputs(t));
        REQUIRE(client.HasInputs(t));
        REQUIRE(host.GetInput(1, t).buttons == uint8_t(t + 101));// client's inputs
        REQUIRE(client.GetInput(0, t).buttons == uint8_t(t + 1));// host's inputs
    }
    std::printf("input exchange ok (%d ticks, both directions)\n", kTicks);

    // ── 4: stale-peer rebind ────────────────────────────────────────────────
    // Client "switches networks": old socket dies, a new one appears with a
    // different source port. The relay must hand the slot over once the old
    // address has been silent past kPeerStaleMs.
    client.Shutdown();
    relay.Process(float(UdpRelayServer::kPeerStaleMs) / 1000.0f + 1.0f);// fast-forward relay clock
    g_nowMs += UdpRelayServer::kPeerStaleMs + 1000;
    // The fast-forward made BOTH peers look stale to the relay. Let the host
    // pump a few steps (it keeps sending inputs/pings) so its lastSeen is
    // fresh again and only the dead client's slot is the stale one.
    for (int i = 0; i < 10; i++)
        step(relay, &host, nullptr);

    LockstepNet client2;
    REQUIRE(client2.StartRelayClient("127.0.0.1", port, kRoomId));
    for (int i = 0; i < 2000 && client2.state != LockstepNet::State::Running; i++)
        step(relay, &host, &client2);
    REQUIRE(client2.state == LockstepNet::State::Running);
    std::printf("stale-peer rebind ok\n");

    host.Shutdown();
    client2.Shutdown();
    relay.Stop();
    std::printf("RelayLoopbackTest: all checks passed\n");
    return 0;
}

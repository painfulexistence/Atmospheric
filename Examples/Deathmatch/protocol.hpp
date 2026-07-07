#pragma once
#include <cstdint>
#include <cstring>

// Wire protocol for Deathmatch — a 2-player, server-authoritative, Quake-style
// arena shooter whose whole reason to exist is demonstrating lag compensation
// and the favor-the-shooter vs favor-the-target tradeoff (see authority.hpp).
//
// Client-server, like HideAndSeek and unlike MultiplayerSandbox's lockstep:
// clients send input, the server is the sole simulator, clients receive
// snapshots. Two things this protocol carries that HideAndSeek's didn't:
//
//   1. renderTick — the server tick the client was actually *looking at* when
//      it fired (its latest received snapshot tick minus the interpolation
//      delay). The server rewinds targets to this tick for hit detection.
//      That single field is the entire mechanism of favor-the-shooter lag
//      compensation: the server judges the shot against the world the shooter
//      saw, not the newer world the server has moved on to.
//
//   2. per-weapon fire sequence numbers (railSeq / rocketSeq) — a fire is an
//      *event*, and losing it is unacceptable (a dropped "I shot" packet is a
//      shot that never happened). Unlike movement, which HideAndSeek could let
//      go stale for a tick, each fire is repeated in every input packet until
//      the next fire supersedes it; the server processes one when its sequence
//      advances and dedups the repeats. A minimal reliable-events-over-UDP
//      layer, distinct from the reliable-*stream* redundancy MultiplayerSandbox
//      uses for lockstep inputs.
namespace proto {

    constexpr uint32_t kMagic = 0x444D4131;// "DMA1"

    enum class PacketType : uint8_t {
        ClientHello = 1,// client -> server: join a slot
        ServerWelcome = 2,// server -> client: {playerId, tickRateHz}
        ClientInput = 3,// client -> server: {clientTick, renderTick, move, fires}
        ServerSnapshot = 4,// server -> client: authoritative state for this client
    };

    // ── byte helpers (little-endian) ─────────────────────────────────────────
    inline void PutU16(uint8_t* p, uint16_t v) {
        p[0] = static_cast<uint8_t>(v);
        p[1] = static_cast<uint8_t>(v >> 8);
    }
    inline uint16_t GetU16(const uint8_t* p) {
        return static_cast<uint16_t>(p[0]) | static_cast<uint16_t>(p[1]) << 8;
    }
    inline void PutU32(uint8_t* p, uint32_t v) {
        p[0] = static_cast<uint8_t>(v);
        p[1] = static_cast<uint8_t>(v >> 8);
        p[2] = static_cast<uint8_t>(v >> 16);
        p[3] = static_cast<uint8_t>(v >> 24);
    }
    inline uint32_t GetU32(const uint8_t* p) {
        return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16
               | static_cast<uint32_t>(p[3]) << 24;
    }
    inline void PutF32(uint8_t* p, float v) {
        std::memcpy(p, &v, sizeof(float));
    }
    inline float GetF32(const uint8_t* p) {
        float v;
        std::memcpy(&v, p, sizeof(float));
        return v;
    }

    // Direction axis quantized to a signed byte (-127..127 == -1..1). The
    // server owns speed/damage, so lying about magnitude here buys nothing.
    inline int8_t QuantizeAxis(float v) {
        if (v > 1.0f) v = 1.0f;
        if (v < -1.0f) v = -1.0f;
        return static_cast<int8_t>(v * 127.0f);
    }
    inline float DequantizeAxis(int8_t v) {
        return static_cast<float>(v) / 127.0f;
    }

    // ── packet layouts ───────────────────────────────────────────────────────
    // ClientHello: magic(4) type(1)
    constexpr int kClientHelloLen = 5;

    // ServerWelcome: magic(4) type(1) playerId(1) tickRateHz(f32)
    constexpr int kServerWelcomeLen = 5 + 1 + 4;

    // ClientInput: magic(4) type(1)
    //   clientTick(u32) renderTick(u32)
    //   moveX(i8) moveY(i8)
    //   railSeq(u16)   railAimX(i8)   railAimY(i8)
    //   rocketSeq(u16) rocketAimX(i8) rocketAimY(i8)
    constexpr int kClientInputLen = 5 + 4 + 4 + 1 + 1 + (2 + 1 + 1) + (2 + 1 + 1);

    // ServerSnapshot header: magic(4) type(1)
    //   serverTick(u32) ackedInputTick(u32)
    //   selfX(f32) selfY(f32) selfHealth(u8) selfAlive(u8)
    //   selfScore(u8) enemyScore(u8)
    //   enemyAlive(u8) enemyX(f32) enemyY(f32) enemyHealth(u8)
    //   numRockets(u8)
    // then numRockets * rocket entry (below)
    constexpr int kServerSnapshotHeaderLen = 5 + 4 + 4 + 4 + 4 + 1 + 1 + 1 + 1 + 1 + 4 + 4 + 1 + 1;

    // Rocket entry: id(u16) ownerId(u8) x(f32) y(f32)
    constexpr int kRocketEntryLen = 2 + 1 + 4 + 4;
    constexpr int kMaxRocketsInSnapshot = 16;

}// namespace proto

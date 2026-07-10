#pragma once
#include "sim_common.hpp"
#include <cstdint>
#include <cstring>

// Wire protocol for Deathmatch — a 2-player, server-authoritative, Quake-style
// 3D arena shooter built to demonstrate lag compensation, the favor-the-shooter
// vs favor-the-target tradeoff, and (new in the 3D version) view-angle
// replication and quantization.
//
// Things this protocol carries that a 2D version doesn't:
//
//   1. View angles (yaw/pitch), quantized. Every real FPS replicates where
//      each player is looking, and packs it tightly — Quake used a single byte
//      per angle (~1.4° steps), Source uses ~16 bits. We use 16-bit yaw and
//      16-bit pitch (~0.005° steps): visually lossless, still a fraction of a
//      float. The client's *own* view is never sent back to it (it owns its
//      camera); the enemy's yaw/pitch are replicated so its model faces the
//      right way and can be interpolated.
//
//   2. renderTick — the server tick the client was looking at when it fired
//      (latest snapshot tick minus interpolation delay). The server rewinds
//      the target to renderTick for hit detection: favor-the-shooter lag comp.
//
//   3. Per-weapon fire sequences with a *latched* view angle. A fire is an
//      event (losing it = a shot that never happened), so each fire's sequence
//      AND the yaw/pitch it was fired at are repeated in every input packet
//      until the next fire supersedes them — surviving packet loss without the
//      shot direction drifting as the player keeps turning.
namespace proto {

    constexpr uint32_t kMagic = 0x444D4132;// "DMA2" (3D revision)

    enum class PacketType : uint8_t {
        ClientHello = 1,
        ServerWelcome = 2,
        ClientInput = 3,
        ServerSnapshot = 4,
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
    inline void PutVec3(uint8_t* p, sim::Vec3 v) {
        PutF32(p, v.x);
        PutF32(p + 4, v.y);
        PutF32(p + 8, v.z);
    }
    inline sim::Vec3 GetVec3(const uint8_t* p) {
        return { GetF32(p), GetF32(p + 4), GetF32(p + 8) };
    }

    // Angle quantization. Yaw over [0, 2π) into 16 bits; pitch over
    // [-π/2, π/2] into 16 bits.
    inline uint16_t QuantizeYaw(float yaw) {
        float t = std::fmod(yaw, sim::kTwoPi);
        if (t < 0.0f) t += sim::kTwoPi;
        return static_cast<uint16_t>((t / sim::kTwoPi) * 65535.0f + 0.5f);
    }
    inline float DequantizeYaw(uint16_t q) {
        return (static_cast<float>(q) / 65535.0f) * sim::kTwoPi;
    }
    inline uint16_t QuantizePitch(float pitch) {
        float t = sim::Clamp(pitch, -sim::kPi * 0.5f, sim::kPi * 0.5f);
        return static_cast<uint16_t>(((t + sim::kPi * 0.5f) / sim::kPi) * 65535.0f + 0.5f);
    }
    inline float DequantizePitch(uint16_t q) {
        return (static_cast<float>(q) / 65535.0f) * sim::kPi - sim::kPi * 0.5f;
    }

    // Movement forward/strafe quantized to a signed byte (-127..127 == -1..1).
    inline int8_t QuantizeAxis(float v) {
        if (v > 1.0f) v = 1.0f;
        if (v < -1.0f) v = -1.0f;
        return static_cast<int8_t>(v * 127.0f);
    }
    inline float DequantizeAxis(int8_t v) {
        return static_cast<float>(v) / 127.0f;
    }

    // Ability button bits in the ClientInput `buttons` byte.
    constexpr uint8_t kBtnJump = 1 << 0;// tap grounded = jump; hold airborne = levitate
    constexpr uint8_t kBtnDash = 1 << 1;
    constexpr uint8_t kBtnShield = 1 << 2;

    // ── packet layouts ───────────────────────────────────────────────────────
    // ClientHello: magic(4) type(1)
    constexpr int kClientHelloLen = 5;

    // ServerWelcome: magic(4) type(1) playerId(1) tickRateHz(f32)
    constexpr int kServerWelcomeLen = 5 + 1 + 4;

    // ClientInput: magic(4) type(1)
    //   clientTick(u32) renderTick(u32)
    //   forward(i8) strafe(i8) buttons(u8) viewYaw(u16) viewPitch(u16)
    //   railSeq(u16)   railYaw(u16)   railPitch(u16)
    //   rocketSeq(u16) rocketYaw(u16) rocketPitch(u16)
    constexpr int kClientInputLen = 5 + 4 + 4 + 1 + 1 + 1 + 2 + 2 + (2 + 2 + 2) + (2 + 2 + 2);

    // ServerSnapshot header: magic(4) type(1)
    //   serverTick(u32) ackedInputTick(u32)
    //   selfFoot(vec3) selfVy(f32) selfDashCd(u8) selfDashTicks(u8)
    //   selfHealth(u8) selfAlive(u8) selfShield(u8) selfScore(u8) enemyScore(u8)
    //   enemyAlive(u8) enemyFoot(vec3) enemyYaw(u16) enemyPitch(u16)
    //   enemyHealth(u8) enemyShield(u8) numRockets(u8)
    // then numRockets * rocket entry
    constexpr int kServerSnapshotHeaderLen =
        5 + 4 + 4 + 12 + 4 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 12 + 2 + 2 + 1 + 1 + 1;

    // Rocket entry: id(u16) ownerId(u8) pos(vec3)
    constexpr int kRocketEntryLen = 2 + 1 + 12;
    constexpr int kMaxRocketsInSnapshot = 16;

}// namespace proto

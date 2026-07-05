#pragma once
#include <cstdint>
#include <cstring>

// Wire protocol shared by HiddenTagServer and HiddenTagClient.
//
// Unlike MultiplayerSandbox's lockstep protocol, this is client-server, not
// peer-symmetric: clients send only their input, the server is the sole
// simulator, and each client receives a SNAPSHOT containing only the
// entities its role is allowed to see. Losing a packet just means "use the
// last known value one tick longer" for both directions, so unlike lockstep
// there is no need to redundantly resend a window of history — UDP's
// unreliability is absorbed by the game being tolerant of stale state for a
// tick, not by retransmission.
namespace proto {

    constexpr uint32_t kMagic = 0x48544731;// "HTG1"

    enum class PacketType : uint8_t {
        ClientHello = 1,// client -> server: {role}
        ServerWelcome = 2,// server -> client: {entityId, tickRateHz}
        ClientInput = 3,// client -> server: {tick, dx, dy}
        ServerSnapshot = 4,// server -> client: {serverTick, ackedInputTick, ownX, ownY, visibleCount, [id,x,y]*}
    };

    enum class Role : uint8_t { Seeker = 0, Hider = 1 };

    // Entity ids are fixed: there are exactly two entities in this world.
    constexpr uint8_t kSeekerEntityId = 0;
    constexpr uint8_t kHiderEntityId = 1;

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

    // Movement direction is sent as a unit-ish vector quantized to signed
    // bytes (-127..127 == -1.0f..1.0f); the server owns speed and tick delta,
    // so a modified client gains nothing by lying about magnitude here.
    inline int8_t QuantizeAxis(float v) {
        if (v > 1.0f) v = 1.0f;
        if (v < -1.0f) v = -1.0f;
        return static_cast<int8_t>(v * 127.0f);
    }
    inline float DequantizeAxis(int8_t v) {
        return static_cast<float>(v) / 127.0f;
    }

    constexpr int kClientHelloLen = 5 + 1;// magic+type, role
    constexpr int kServerWelcomeLen = 5 + 1 + 4;// magic+type, entityId, tickRateHz (f32)
    constexpr int kClientInputLen = 5 + 4 + 1 + 1;// magic+type, tick, dx, dy
    constexpr int kServerSnapshotHeaderLen = 5 + 4 + 4 + 4 + 4 + 1;// magic+type,serverTick,ackedTick,ownX,ownY,count
    constexpr int kSnapshotEntryLen = 1 + 4 + 4;// id, x, y
    constexpr int kMaxVisibleEntities = 4;// generous headroom; this world only ever has 1

}// namespace proto

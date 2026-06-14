#pragma once
#include "sand_world.hpp"
#include <cstdint>
#include <vector>

// Fixed-rate, deterministic game simulation on top of SandWorld.
// Both peers run this bit-identically; only InputFrames cross the network.

constexpr int TICK_RATE = 60;
constexpr float TICK_DT = 1.0f / TICK_RATE;

enum InputButtons : uint8_t {
    BTN_LEFT = 1 << 0,
    BTN_RIGHT = 1 << 1,
    BTN_JUMP = 1 << 2,
    BTN_FIRE = 1 << 3,
    BTN_DOWN = 1 << 4,
};

struct InputFrame {
    uint8_t buttons = 0;
    uint8_t spell = 0;
    int16_t aimQ = 0;// aim angle in radians * 10000

    float Aim() const {
        return float(aimQ) / 10000.0f;
    }
    static int16_t QuantizeAim(float a) {
        return int16_t(a * 10000.0f);
    }
    bool operator==(const InputFrame& o) const {
        return buttons == o.buttons && spell == o.spell && aimQ == o.aimQ;
    }
};

enum class SpellType : uint8_t {
    SparkBolt = 0,// fast light bolt
    Fireball,// explodes + ignites
    Grenade,// bouncing bomb on a fuse
    WaterJet,// sprays water cells
    AcidFlask,// lobbed vial, splashes acid
    DigBlast,// hitscan terrain excavation
    RapidWand,// low-damage automatic wand
    Count
};

struct SpellDef {
    const char* name;
    const char* key;
    int cooldownTicks;
};
extern const SpellDef kSpells[int(SpellType::Count)];

struct Player {
    float x = 0, y = 0;// center, in cell units
    float vx = 0, vy = 0;// cells per tick
    int hp = 100;
    bool alive = true;
    bool grounded = false;
    int levitation = 0;
    uint32_t respawnAt = 0;
    uint32_t cooldownUntil = 0;
    int kills = 0;
    int deaths = 0;
    int lastDamageFrom = -1;

    static constexpr int HW = 2;// hitbox half-width (5 cells wide)
    static constexpr int HH = 5;// hitbox half-height (11 cells tall)
    static constexpr int MAX_HP = 100;
    static constexpr int LEVI_MAX = 90;
};

struct Projectile {
    bool alive = false;
    SpellType type = SpellType::SparkBolt;
    uint8_t owner = 0;
    float x = 0, y = 0;
    float vx = 0, vy = 0;
    int life = 0;
};

class GameSim {
public:
    SandWorld world;
    Player players[2];
    std::vector<Projectile> projectiles;
    uint32_t tick = 0;

    void Init(uint32_t seed);
    void Step(const InputFrame& in0, const InputFrame& in1);
    uint32_t Checksum() const;

private:
    uint32_t rng = 1;

    inline uint32_t Rand() {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return rng;
    }
    // Uniform float in [-s, s], deterministic
    inline float RandSym(float s) {
        return (float(Rand() & 0xFFFF) - 32768.0f) / 32768.0f * s;
    }

    void StepPlayer(int idx, const InputFrame& in);
    void ApplyEnvironmentDamage(int idx);
    void FireSpell(int idx, const InputFrame& in);
    void SpawnProjectile(SpellType type, int owner, float x, float y, float vx, float vy, int life);
    void StepProjectiles();
    void ImpactProjectile(Projectile& p);
    void Explode(float cx, float cy, int radius, int damage, int owner, bool ignite);
    void DamagePlayer(int idx, int dmg, int from, float kx, float ky);
    bool PlayerBoxBlocked(int idx, float px, float py) const;
};

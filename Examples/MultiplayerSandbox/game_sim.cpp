#include "game_sim.hpp"
#include <algorithm>
#include <cmath>

const SpellDef kSpells[int(SpellType::Count)] = {
    { "Spark Bolt", "1", 10 },
    { "Fireball", "2", 50 },
    { "Grenade", "3", 100 },
    { "Water Jet", "4", 4 },
    { "Acid Flask", "5", 90 },
    { "Dig Blast", "6", 12 },
    { "Rapid Wand", "7", 5 },
};

namespace {
constexpr float GRAVITY = 0.07f;
constexpr float MAX_FALL = 2.0f;
constexpr float WALK_ACCEL = 0.18f;
constexpr float WALK_MAX = 0.9f;
constexpr float JUMP_VEL = -1.7f;
constexpr float LEVI_THRUST = 0.16f;
constexpr float LEVI_MAX_RISE = -1.2f;
constexpr uint32_t RESPAWN_TICKS = 180;
}

void GameSim::Init(uint32_t seed) {
    rng = (seed ^ 0x9e3779b9u) | 1u;
    tick = 0;
    world.Generate(seed);
    projectiles.clear();
    projectiles.reserve(256);
    for (int i = 0; i < 2; i++) {
        players[i] = Player{};
        players[i].x = float(world.spawnX[i]);
        players[i].y = float(world.spawnY[i]);
        players[i].hp = Player::MAX_HP;
        players[i].levitation = Player::LEVI_MAX;
    }
}

void GameSim::Step(const InputFrame& in0, const InputFrame& in1) {
    StepPlayer(0, in0);
    StepPlayer(1, in1);
    StepProjectiles();
    world.Step(tick);
    tick++;
}

// ---------------------------------------------------------------------------
// Player
// ---------------------------------------------------------------------------

bool GameSim::PlayerBoxBlocked(int idx, float px, float py) const {
    int x0 = int(std::floor(px)) - Player::HW;
    int x1 = int(std::floor(px)) + Player::HW;
    int y0 = int(std::floor(py)) - Player::HH;
    int y1 = int(std::floor(py)) + Player::HH;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            if (world.SolidAt(x, y)) return true;
        }
    }
    return false;
}

void GameSim::StepPlayer(int idx, const InputFrame& in) {
    Player& p = players[idx];

    if (!p.alive) {
        if (tick >= p.respawnAt) {
            p.alive = true;
            p.hp = Player::MAX_HP;
            p.x = float(world.spawnX[idx]);
            p.y = float(world.spawnY[idx]);
            p.vx = p.vy = 0;
            p.levitation = Player::LEVI_MAX;
            p.lastDamageFrom = -1;
            world.CarveCircle(world.spawnX[idx], world.spawnY[idx], 8);
        }
        return;
    }

    bool inLiquid = SandWorld::IsLiquid(world.GetMat(int(p.x), int(p.y)));

    // Horizontal movement
    if (in.buttons & BTN_LEFT) p.vx -= WALK_ACCEL;
    if (in.buttons & BTN_RIGHT) p.vx += WALK_ACCEL;
    if (!(in.buttons & (BTN_LEFT | BTN_RIGHT))) p.vx *= p.grounded ? 0.72f : 0.92f;
    p.vx = std::clamp(p.vx, -WALK_MAX, WALK_MAX);

    // Vertical: gravity / jump / levitation / swimming
    if (inLiquid) {
        p.vy *= 0.90f;
        p.vy += GRAVITY * 0.35f;
        if (in.buttons & BTN_JUMP) p.vy -= 0.13f;
    } else {
        p.vy += GRAVITY;
        if ((in.buttons & BTN_JUMP)) {
            if (p.grounded) {
                p.vy = JUMP_VEL;
                p.grounded = false;
            } else if (p.levitation > 0) {
                p.vy -= LEVI_THRUST;
                p.levitation--;
                if (p.vy < LEVI_MAX_RISE) p.vy = LEVI_MAX_RISE;
            }
        }
    }
    p.vy = std::min(p.vy, MAX_FALL);

    // Axis-separated movement in sub-cell steps to avoid tunneling
    float remX = p.vx;
    while (std::abs(remX) > 1e-4f) {
        float step = std::clamp(remX, -0.9f, 0.9f);
        if (!PlayerBoxBlocked(idx, p.x + step, p.y)) {
            p.x += step;
        } else {
            // try stepping up small ledges (1..3 cells)
            bool climbed = false;
            for (int up = 1; up <= 3; up++) {
                if (!PlayerBoxBlocked(idx, p.x + step, p.y - float(up))) {
                    p.x += step;
                    p.y -= float(up);
                    climbed = true;
                    break;
                }
            }
            if (!climbed) {
                p.vx = 0;
                break;
            }
        }
        remX -= step;
    }

    p.grounded = false;
    float remY = p.vy;
    while (std::abs(remY) > 1e-4f) {
        float step = std::clamp(remY, -0.9f, 0.9f);
        if (!PlayerBoxBlocked(idx, p.x, p.y + step)) {
            p.y += step;
        } else {
            if (step > 0) p.grounded = true;
            p.vy = 0;
            break;
        }
        remY -= step;
    }

    if (p.grounded) {
        p.levitation = std::min(p.levitation + 3, Player::LEVI_MAX);
    }

    p.x = std::clamp(p.x, 3.0f, float(SandWorld::W - 4));
    p.y = std::clamp(p.y, 3.0f, float(SandWorld::H - 4));

    ApplyEnvironmentDamage(idx);

    if ((in.buttons & BTN_FIRE) && tick >= p.cooldownUntil) {
        FireSpell(idx, in);
    }
}

void GameSim::ApplyEnvironmentDamage(int idx) {
    Player& p = players[idx];
    bool touchLava = false, touchFire = false, touchAcid = false;
    int x0 = int(p.x) - Player::HW, x1 = int(p.x) + Player::HW;
    int y0 = int(p.y) - Player::HH, y1 = int(p.y) + Player::HH;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            Mat m = world.GetMat(x, y);
            touchLava |= (m == Mat::Lava);
            touchFire |= (m == Mat::Fire);
            touchAcid |= (m == Mat::Acid);
        }
    }
    if (touchLava && tick % 4 == 0) DamagePlayer(idx, 3, -1, 0, -0.1f);
    if (touchFire && tick % 8 == 0) DamagePlayer(idx, 1, -1, 0, 0);
    if (touchAcid && tick % 6 == 0) DamagePlayer(idx, 2, -1, 0, 0);
}

void GameSim::DamagePlayer(int idx, int dmg, int from, float kx, float ky) {
    Player& p = players[idx];
    if (!p.alive) return;
    p.hp -= dmg;
    p.vx += kx;
    p.vy += ky;
    if (from >= 0) p.lastDamageFrom = from;
    if (p.hp <= 0) {
        p.hp = 0;
        p.alive = false;
        p.deaths++;
        p.respawnAt = tick + RESPAWN_TICKS;
        if (p.lastDamageFrom >= 0 && p.lastDamageFrom != idx) {
            players[p.lastDamageFrom].kills++;
        }
        // Death burst: a small splash of smoke + carve
        world.CarveCircle(int(p.x), int(p.y), 4);
        world.PaintCircle(int(p.x), int(p.y), 4, Mat::Smoke, 120);
    }
}

// ---------------------------------------------------------------------------
// Spells
// ---------------------------------------------------------------------------

void GameSim::SpawnProjectile(SpellType type, int owner, float x, float y, float vx, float vy, int life) {
    for (Projectile& p : projectiles) {
        if (!p.alive) {
            p = Projectile{ true, type, uint8_t(owner), x, y, vx, vy, life };
            return;
        }
    }
    projectiles.push_back(Projectile{ true, type, uint8_t(owner), x, y, vx, vy, life });
}

void GameSim::FireSpell(int idx, const InputFrame& in) {
    Player& p = players[idx];
    SpellType spell = SpellType(in.spell % uint8_t(SpellType::Count));
    float a = in.Aim();
    float dx = std::cos(a), dy = std::sin(a);
    float mx = p.x + dx * 6.0f;
    float my = p.y + dy * 6.0f;
    p.cooldownUntil = tick + uint32_t(kSpells[int(spell)].cooldownTicks);

    switch (spell) {
    case SpellType::SparkBolt:
        SpawnProjectile(spell, idx, mx, my, dx * 4.5f, dy * 4.5f, 120);
        break;
    case SpellType::Fireball:
        SpawnProjectile(spell, idx, mx, my, dx * 2.4f, dy * 2.4f, 240);
        break;
    case SpellType::Grenade:
        SpawnProjectile(spell, idx, mx, my, dx * 2.8f, dy * 2.8f - 0.3f, 100);
        break;
    case SpellType::WaterJet:
        for (int i = 0; i < 2; i++) {
            SpawnProjectile(
              spell, idx, mx, my, dx * 3.0f + RandSym(0.25f), dy * 3.0f + RandSym(0.25f), 60
            );
        }
        break;
    case SpellType::AcidFlask:
        SpawnProjectile(spell, idx, mx, my, dx * 2.6f, dy * 2.6f - 0.2f, 300);
        break;
    case SpellType::DigBlast: {
        int hx, hy;
        world.Raycast(p.x, p.y, dx, dy, 40, hx, hy);
        world.CarveCircle(hx, hy, 5);
        break;
    }
    case SpellType::RapidWand: {
        float s = RandSym(0.07f);
        float ca = a + s;
        SpawnProjectile(spell, idx, mx, my, std::cos(ca) * 5.0f, std::sin(ca) * 5.0f, 90);
        // recoil
        p.vx -= dx * 0.05f;
        break;
    }
    default: break;
    }
}

// ---------------------------------------------------------------------------
// Projectiles
// ---------------------------------------------------------------------------

void GameSim::StepProjectiles() {
    for (Projectile& p : projectiles) {
        if (!p.alive) continue;

        if (--p.life <= 0) {
            if (p.type == SpellType::Grenade) {
                Explode(p.x, p.y, 16, 60, p.owner, false);
            } else if (p.type == SpellType::WaterJet) {
                world.PaintCircle(int(p.x), int(p.y), 2, Mat::Water, 230);
            } else if (p.type == SpellType::Fireball) {
                Explode(p.x, p.y, 11, 38, p.owner, true);
            }
            p.alive = false;
            continue;
        }

        // gravity per projectile type
        switch (p.type) {
        case SpellType::Fireball: p.vy += 0.015f; break;
        case SpellType::Grenade: p.vy += 0.06f; break;
        case SpellType::WaterJet: p.vy += 0.05f; break;
        case SpellType::AcidFlask: p.vy += 0.05f; break;
        default: break;
        }

        float speed = std::max(std::abs(p.vx), std::abs(p.vy));
        int substeps = std::max(1, int(std::ceil(speed)));
        float sx = p.vx / float(substeps);
        float sy = p.vy / float(substeps);

        for (int s = 0; s < substeps && p.alive; s++) {
            float nx = p.x + sx;
            float ny = p.y + sy;

            // hit players
            for (int i = 0; i < 2; i++) {
                Player& pl = players[i];
                if (!pl.alive || i == int(p.owner)) continue;
                if (std::abs(nx - pl.x) <= Player::HW + 1.5f && std::abs(ny - pl.y) <= Player::HH + 1.5f) {
                    switch (p.type) {
                    case SpellType::SparkBolt:
                        DamagePlayer(i, 10, p.owner, p.vx * 0.15f, p.vy * 0.15f - 0.2f);
                        break;
                    case SpellType::RapidWand:
                        DamagePlayer(i, 4, p.owner, p.vx * 0.06f, p.vy * 0.06f);
                        break;
                    case SpellType::Fireball: Explode(nx, ny, 11, 38, p.owner, true); break;
                    case SpellType::AcidFlask:
                        DamagePlayer(i, 8, p.owner, p.vx * 0.1f, p.vy * 0.1f);
                        world.PaintCircle(int(nx), int(ny), 5, Mat::Acid, 200);
                        break;
                    case SpellType::WaterJet:
                        world.PaintCircle(int(nx), int(ny), 2, Mat::Water, 230);
                        pl.vx += p.vx * 0.1f;// water pushes
                        break;
                    default: break;
                    }
                    if (p.type != SpellType::Grenade) p.alive = false;
                    break;
                }
            }
            if (!p.alive) break;

            // hit terrain
            if (world.SolidAt(int(nx), int(ny))) {
                if (p.type == SpellType::Grenade) {
                    // bounce: pick the axis that hit
                    if (world.SolidAt(int(nx), int(p.y))) {
                        p.vx = -p.vx * 0.5f;
                        sx = -sx * 0.5f;
                    } else {
                        p.vy = -p.vy * 0.45f;
                        sy = -sy * 0.45f;
                        p.vx *= 0.75f;
                        sx *= 0.75f;
                    }
                    continue;
                }
                ImpactProjectile(p);
                p.alive = false;
                break;
            }

            // liquids drag / douse
            Mat at = world.GetMat(int(nx), int(ny));
            if (SandWorld::IsLiquid(at)) {
                p.vx *= 0.92f;
                p.vy *= 0.92f;
                if (p.type == SpellType::Fireball && at == Mat::Water) {
                    world.PaintCircle(int(nx), int(ny), 2, Mat::Steam, 180);
                    p.alive = false;
                    break;
                }
            }

            p.x = nx;
            p.y = ny;
            if (p.x < 1 || p.y < 1 || p.x > SandWorld::W - 2 || p.y > SandWorld::H - 2) {
                p.alive = false;
                break;
            }
        }
    }
}

void GameSim::ImpactProjectile(Projectile& p) {
    int x = int(p.x), y = int(p.y);
    switch (p.type) {
    case SpellType::SparkBolt: world.CarveCircle(x, y, 2); break;
    case SpellType::RapidWand: world.CarveCircle(x, y, 1); break;
    case SpellType::Fireball: Explode(p.x, p.y, 11, 38, p.owner, true); break;
    case SpellType::WaterJet: world.PaintCircle(x, y, 2, Mat::Water, 230); break;
    case SpellType::AcidFlask: world.PaintCircle(x, y, 5, Mat::Acid, 220); break;
    default: break;
    }
}

void GameSim::Explode(float cx, float cy, int radius, int damage, int owner, bool ignite) {
    world.CarveCircle(int(cx), int(cy), radius);
    if (ignite) {
        world.IgniteCircle(int(cx), int(cy), radius + 2, 160);
    }
    world.PaintCircle(int(cx), int(cy), radius / 2, Mat::Smoke, 60);

    float R = float(radius) * 2.0f;
    for (int i = 0; i < 2; i++) {
        Player& pl = players[i];
        if (!pl.alive) continue;
        float dx = pl.x - cx, dy = pl.y - cy;
        float d = std::sqrt(dx * dx + dy * dy);
        if (d >= R) continue;
        float t = 1.0f - d / R;
        int dmg = int(float(damage) * t);
        float inv = (d > 0.5f) ? 1.0f / d : 0.0f;
        DamagePlayer(i, dmg, owner, dx * inv * 2.2f * t, dy * inv * 2.2f * t - 0.6f * t);
    }
}

// ---------------------------------------------------------------------------

uint32_t GameSim::Checksum() const {
    uint32_t h = world.Checksum();
    auto mix = [&h](uint32_t v) {
        h ^= v + 0x9e3779b9u + (h << 6) + (h >> 2);
    };
    for (int i = 0; i < 2; i++) {
        const Player& p = players[i];
        mix(uint32_t(int32_t(p.x * 16.0f)));
        mix(uint32_t(int32_t(p.y * 16.0f)));
        mix(uint32_t(p.hp));
        mix(uint32_t(p.kills * 7 + p.deaths));
        mix(p.alive ? 1u : 0u);
    }
    mix(tick);
    return h;
}

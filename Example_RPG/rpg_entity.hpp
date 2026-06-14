#pragma once
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <cmath>
#include <algorithm>
#include <random>
#include <cstdint>

// ---------------------------------------------------------------------------
// Axis-aligned bounding box helpers
// ---------------------------------------------------------------------------

struct AABB {
    float x, y, w, h;
};

inline bool AABBOverlaps(const AABB& a, const AABB& b) {
    return a.x < b.x + b.w && a.x + a.w > b.x &&
           a.y < b.y + b.h && a.y + a.h > b.y;
}

// ---------------------------------------------------------------------------
// Stats — shared between Player and Enemy combatants
// ---------------------------------------------------------------------------

struct Stats {
    int hp,    maxHp;
    int mp,    maxMp;
    int atk,   def,   spd;

    bool isDead()       const { return hp <= 0; }
    void takeDamage(int dmg)  { hp = std::max(0, hp - std::max(1, dmg)); }
    void heal(int amt)        { hp = std::min(maxHp, hp + amt); }
    void restoreMp(int amt)   { mp = std::min(maxMp, mp + amt); }
};

// ---------------------------------------------------------------------------
// Skill definition
// ---------------------------------------------------------------------------

enum class SkillTarget { Enemy, Self, AllEnemies };

struct Skill {
    std::string  name;
    int          mpCost      = 0;
    SkillTarget  target      = SkillTarget::Enemy;
    std::string  description;

    // Returns damage/heal amount (positive = damage to target, negative = heal)
    std::function<int(const Stats& user, const Stats& target)> calc;
};

// ---------------------------------------------------------------------------
// Item definition
// ---------------------------------------------------------------------------

enum class ItemEffect { HealHP, HealMP };

struct Item {
    std::string name;
    ItemEffect  effect;
    int         amount;
    int         count;
};

// ---------------------------------------------------------------------------
// Base world entity
// ---------------------------------------------------------------------------

struct Entity {
    float x = 0, y = 0;
    float w = 24, h = 24;

    float cx() const { return x + w * 0.5f; }
    float cy() const { return y + h * 0.5f; }

    AABB aabb() const { return { x, y, w, h }; }
};

// ---------------------------------------------------------------------------
// Player
// ---------------------------------------------------------------------------

struct Player : Entity {
    float speed  = 90.0f;
    float facing = 1.0f;   // +1 right, -1 left
    bool  moving = false;

    Stats              stats  = { 80, 80, 30, 30, 18, 10, 12 };
    std::vector<Skill> skills;
    std::vector<Item>  items;

    void initSkills() {
        skills.push_back({
            "Fire", 8, SkillTarget::Enemy,
            "Deal heavy fire damage",
            [](const Stats& u, const Stats&) { return u.atk * 2 + 5; },
        });
        skills.push_back({
            "Heal", 10, SkillTarget::Self,
            "Restore 25% max HP",
            [](const Stats&, const Stats& t) { return -(t.maxHp / 4); },
        });
        skills.push_back({
            "Blaze", 14, SkillTarget::AllEnemies,
            "Hit all enemies with fire",
            [](const Stats& u, const Stats&) { return u.atk + 8; },
        });
    }

    void initItems() {
        items.push_back({ "Potion", ItemEffect::HealHP, 30, 3 });
        items.push_back({ "Ether",  ItemEffect::HealMP, 15, 2 });
    }
};

// ---------------------------------------------------------------------------
// Enemy definition (archetype) and world instance
// ---------------------------------------------------------------------------

struct EnemyDef {
    std::string name;
    Stats       stats;
    int         expReward;
    int         goldReward;

    // Returns skill index to use this turn, or -1 for basic attack
    std::function<int(const Stats& self, const Stats& player)> chooseAction;
};

struct Enemy : Entity {
    Enemy() = default;
    Enemy(float startX, float startY, EnemyDef def)
        : def(std::move(def)) { x = startX; y = startY; w = h = 24; }

    EnemyDef def;
    bool     alive     = true;
    bool     aggro     = false;
    float    aggroR    = 120.0f;

    // Per-battle HP/MP snapshot (reset from def.stats at battle start)
    Stats    battle    = {};

    void resetBattleStats() { battle = def.stats; }
};

// ---------------------------------------------------------------------------
// NPC
// ---------------------------------------------------------------------------

struct NPC : Entity {
    NPC() = default;
    NPC(float nx, float ny, std::string n, std::vector<std::string> lines)
        : name(std::move(n)), dialogue(std::move(lines)) { x = nx; y = ny; w = h = 24; }

    std::string              name;
    std::vector<std::string> dialogue;
    int                      dialogIdx  = 0;
    float                    talkR      = 48.0f;
};

// ---------------------------------------------------------------------------
// Simple sprite animator (UV-offset, no engine component required)
// ---------------------------------------------------------------------------

struct AnimFrame {
    int   col, row;
    float duration;
};

struct AnimClip {
    std::vector<AnimFrame> frames;
    bool loop = true;
};

struct SpriteAnimator {
    std::unordered_map<std::string, AnimClip> clips;
    std::string currentClip;
    int   frameIdx = 0;
    float timer    = 0.0f;
    bool  playing  = false;

    void addClip(const std::string& name, AnimClip clip) {
        clips[name] = std::move(clip);
    }

    void play(const std::string& name) {
        if (currentClip == name && playing) return;
        currentClip = name;
        frameIdx    = 0;
        timer       = 0.0f;
        playing     = true;
    }

    void update(float dt) {
        if (!playing || clips.empty()) return;
        auto it = clips.find(currentClip);
        if (it == clips.end()) return;
        const auto& clip = it->second;
        if (clip.frames.empty()) return;

        timer += dt;
        if (timer >= clip.frames[frameIdx].duration) {
            timer -= clip.frames[frameIdx].duration;
            frameIdx++;
            if (frameIdx >= (int)clip.frames.size()) {
                if (clip.loop) frameIdx = 0;
                else { frameIdx = (int)clip.frames.size() - 1; playing = false; }
            }
        }
    }

    // Returns {col, row} of the current frame (for UV lookup in DrawSprite2D)
    std::pair<int,int> currentFrame() const {
        auto it = clips.find(currentClip);
        if (it == clips.end() || it->second.frames.empty()) return {0, 0};
        const auto& f = it->second.frames[frameIdx];
        return {f.col, f.row};
    }
};

// ---------------------------------------------------------------------------
// Turn-based battle state
// ---------------------------------------------------------------------------

enum class BattlePhase {
    PlayerMenu,       // main command menu
    PlayerSkillMenu,  // skill sub-menu
    PlayerItemMenu,   // item sub-menu
    EnemyAction,      // enemy acting (brief delay before resolving)
    Victory,          // all enemies defeated
    Defeat,           // player HP reached 0
    Flee,             // running away
};

enum class BattleMenuSel { Attack = 0, Skills, Items, Run, COUNT };

struct BattleLogEntry {
    std::string text;
    float       ttl;   // time-to-live in seconds
};

struct BattleState {
    BattlePhase phase = BattlePhase::PlayerMenu;

    Stats*                   playerStats  = nullptr;  // points into Player::stats
    std::vector<Stats>       enemyStats;              // snapshot copied at battle start
    std::vector<std::string> enemyNames;
    std::vector<int>         enemyIndices;            // index into world _enemies array

    int   targetIdx  = 0;   // which enemy is highlighted
    int   menuSel    = 0;   // main menu cursor
    int   skillSel   = 0;
    int   itemSel    = 0;

    float actionTimer   = 0.0f;  // delay before executing the next phase
    bool  playerFirst   = true;  // who acts first (determined by SPD)

    std::vector<BattleLogEntry> log;
    int   expGained  = 0;
    int   goldGained = 0;

    // Persistent across battles
    int   level      = 1;
    int   exp        = 0;
    int   expToNext  = 20;
    int   gold       = 0;

    void pushLog(std::string msg, float ttl = 2.5f) {
        log.push_back({ std::move(msg), ttl });
        if (log.size() > 5) log.erase(log.begin());
    }

    void tickLog(float dt) {
        for (auto& e : log) e.ttl -= dt;
        log.erase(std::remove_if(log.begin(), log.end(),
            [](const BattleLogEntry& e) { return e.ttl <= 0; }), log.end());
    }

    bool allEnemiesDead() const {
        for (const auto& s : enemyStats)
            if (!s.isDead()) return false;
        return true;
    }
};

// ---------------------------------------------------------------------------
// Procedural texture helpers (returns raw RGBA pixel buffer)
// ---------------------------------------------------------------------------

inline std::vector<uint8_t> MakeColorSheetPixels(
        int tileSize, int cols, int rows,
        const std::vector<std::array<uint8_t, 3>>& colors) {
    int W = cols * tileSize, H = rows * tileSize;
    std::vector<uint8_t> px(W * H * 4);
    static std::mt19937 rng(42);
    std::uniform_int_distribution<int> noise(-5, 5);

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            int idx = r * cols + c;
            auto [cr, cg, cb] = (idx < (int)colors.size()) ? colors[idx]
                                : std::array<uint8_t, 3>{128, 128, 128};
            for (int py = 0; py < tileSize; py++) {
                for (int px_ = 0; px_ < tileSize; px_++) {
                    int i = ((r * tileSize + py) * W + c * tileSize + px_) * 4;
                    bool edge = px_ < 2 || py < 2 || px_ >= tileSize - 2 || py >= tileSize - 2;
                    px[i  ] = (uint8_t)std::clamp((int)cr + (edge ? -30 : noise(rng)), 0, 255);
                    px[i+1] = (uint8_t)std::clamp((int)cg + (edge ? -30 : noise(rng)), 0, 255);
                    px[i+2] = (uint8_t)std::clamp((int)cb + (edge ? -30 : noise(rng)), 0, 255);
                    px[i+3] = 255;
                }
            }
        }
    }
    return px;
}

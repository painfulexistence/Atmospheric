#include "Atmospheric.hpp"

#include <vector>
#include <algorithm>
#include <cmath>
#include <random>
#include <sstream>
#include <iomanip>
#include <string>

// ─────────────────────────────────────────────────────────────
// Wave data: 24 waves × 5 rows × 8 cols  (0=empty,1-3=enemy type)
// Source: painfulexistence/midnight-skyraiders
// ─────────────────────────────────────────────────────────────
static const int WAVES[24][5][8] = {
    // Easy 0-7
    {{0,1,1,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,1,1,0,0,0,0,0}},
    {{0,0,1,0,0,0,0,0},{0,1,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,1,0,0,0,0,0,0},{0,0,1,0,0,0,0,0}},
    {{0,1,0,1,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,1,0,1,0,0,0,0}},
    {{0,0,1,0,0,0,0,0},{0,1,0,0,0,0,0,0},{1,0,0,0,0,0,0,0},{0,1,0,0,0,0,0,0},{0,0,1,0,0,0,0,0}},
    {{0,0,1,0,0,0,0,0},{0,0,1,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,1,0,0,0,0,0},{0,0,1,0,0,0,0,0}},
    {{1,0,0,0,0,0,0,0},{0,1,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,1,0,0,0,0,0,0},{0,0,1,0,0,0,0,0}},
    {{0,0,1,0,0,0,0,0},{0,1,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,1,0,0,0,0,0,0},{1,0,0,0,0,0,0,0}},
    {{0,0,0,0,0,0,0,0},{1,0,1,0,0,0,0,0},{0,0,0,0,0,0,0,0},{1,0,1,0,0,0,0,0},{0,0,0,0,0,0,0,0}},
    // Medium 8-18
    {{0,0,1,0,1,0,0,0},{0,1,0,1,0,0,0,0},{1,0,1,0,0,0,0,0},{0,1,0,1,0,0,0,0},{0,0,1,0,1,0,0,0}},
    {{0,0,1,0,2,0,0,0},{0,1,0,1,0,0,0,0},{1,0,1,0,0,0,0,0},{0,1,0,1,0,0,0,0},{0,0,1,0,2,0,0,0}},
    {{0,0,0,0,0,0,0,0},{0,1,0,0,1,0,0,0},{1,1,1,1,1,1,0,0},{0,1,0,0,1,0,0,0},{0,0,0,0,0,0,0,0}},
    {{0,0,0,0,0,0,0,0},{0,1,0,0,1,0,0,0},{1,1,1,1,3,1,0,0},{0,1,0,0,1,0,0,0},{0,0,0,0,0,0,0,0}},
    {{0,1,1,1,1,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,1,1,1,1,0,0,0}},
    {{0,1,2,1,2,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,2,1,2,1,0,0,0}},
    {{0,2,1,2,1,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,1,2,1,2,0,0,0}},
    {{1,0,0,0,1,0,0,0},{0,1,0,1,0,0,0,0},{0,0,1,0,0,0,0,0},{0,1,0,1,0,0,0,0},{1,0,0,0,1,0,0,0}},
    {{1,0,0,0,1,0,0,0},{0,2,0,2,0,0,0,0},{0,0,3,0,0,0,0,0},{0,2,0,2,0,0,0,0},{1,0,0,0,1,0,0,0}},
    {{0,0,0,0,0,0,0,0},{0,1,1,1,0,0,0,0},{0,1,0,1,0,0,0,0},{0,1,1,1,0,0,0,0},{0,0,0,0,0,0,0,0}},
    {{0,0,0,0,0,0,0,0},{0,1,1,1,0,0,0,0},{0,1,3,1,0,0,0,0},{0,1,1,1,0,0,0,0},{0,0,0,0,0,0,0,0}},
    // Hard 19-23
    {{0,0,0,0,0,0,0,0},{0,2,0,1,0,2,0,1},{0,1,0,1,0,1,0,1},{0,1,0,2,0,1,0,2},{0,0,0,0,0,0,0,0}},
    {{1,0,0,0,0,0,1,0},{0,1,0,1,0,1,0,0},{0,0,1,0,1,0,0,0},{0,1,0,1,0,1,0,0},{1,0,0,0,0,0,1,0}},
    {{1,0,0,0,0,0,1,0},{0,1,0,1,0,1,0,0},{0,0,3,0,3,0,0,0},{0,1,0,1,0,1,0,0},{1,0,0,0,0,0,1,0}},
    {{0,0,1,0,1,0,1,0},{0,1,0,1,0,1,0,0},{0,0,0,0,0,0,0,0},{0,1,0,1,0,1,0,0},{0,0,1,0,1,0,1,0}},
    {{0,0,2,0,2,0,1,0},{0,3,0,1,0,1,0,0},{0,0,0,0,0,0,0,0},{0,3,0,1,0,1,0,0},{0,0,2,0,2,0,1,0}},
};

static std::mt19937 g_rng{ std::random_device{}() };
static float rnd() {
    return std::uniform_real_distribution<float>(0.0f, 1.0f)(g_rng);
}

// World size: 600x600, screen-space coordinates (top-left origin, y-down).
// The original game uses center origin (0,0) with y-down.
// Conversion: eng_x = orig_x + 300;  eng_y = orig_y + 300
static constexpr float WORLD = 600.0f;
static constexpr float HALF  = WORLD * 0.5f;

static float toEngX(float ox) { return ox + HALF; }
static float toEngY(float oy) { return oy + HALF; } // screen-space y-down

enum class GameState { Title, Playing, GameOver };

struct BulletData {
    GameObject* obj = nullptr;
    int   type      = 0;
    float x         = 0.0f;
    float y         = 0.0f;
    float lifetime  = 0.0f;
    float maxLife   = 99999.0f;
    bool  alive     = true;
};

struct EnemyData {
    GameObject* obj      = nullptr;
    int   type           = 1;
    int   level          = 1;
    float x              = 0.0f;
    float y              = 0.0f;
    float speed          = 0.0f;
    float fireCooldown   = 2.0f;
    float fireTimer      = 0.0f;
    float raidDistance   = 0.0f;
    float raidStartTimer = 0.0f;
    float raidEndTimer   = 0.0f;
    float raidDuration   = 0.0f;
    bool  alive          = true;
};

class MidnightSkyraiders : public Application {
    using Application::Application;

    GameState state = GameState::Title;

    // Textures
    GLuint texPlayer  = 0, texEnemy1 = 0, texEnemy2 = 0, texEnemy3  = 0;
    GLuint texBullet  = 0, texCircle = 0, texOrbit  = 0;
    GLuint texEBullet = 0, texECircle = 0;
    GLuint texBg[3]   = {};
    GLuint texTitle   = 0;
    FontID fontID     = 0;

    // Audio
    MusicID bgm     = 0;
    SoundID sfxExp  = 0;
    SoundID sfxOver = 0;

    // Parallax: 3 layers x 2 seamless tiles
    struct BgPair { GameObject* a = nullptr; GameObject* b = nullptr; };
    BgPair bg[3];
    float bgTime = 0.0f;
    // Scroll speeds in px/s: original -0.005, -0.04, -0.12 px/ms x 1000
    static constexpr float BG_SPEED[3] = { 5.0f, 40.0f, 120.0f };

    // Title sprite
    GameObject* titleObj = nullptr;

    // Player (positions are screen-space centers)
    GameObject* playerObj = nullptr;
    // Original top-left (-280, -32), 32x32 -> center (-264, -16) -> screen (36, 284)
    float plX = toEngX(-264.0f);
    float plY = toEngY(-16.0f);
    static constexpr float PL_W = 32.0f, PL_H = 32.0f;
    float fireCooldown = 0.5f;
    float fireTimer    = 0.0f;
    int   plLevel = 1;
    float plXP    = 0.0f;
    float nextXP  = 100.0f;

    // Entities
    std::vector<BulletData> bullets;
    std::vector<EnemyData>  enemies;
    float waveTimer         = 0.0f;
    int   waveCount         = 0;
    float enemySysFireTimer = 0.0f;

    // Score
    double   score   = 0.0;
    int      kills   = 0;
    long long hiScore = 0;

    // ───────────────────────────────────────────
    void OnInit() override {
        GoScene("main", [this]{ OnLoad(); });
    }

    void OnLoad() override {
        // Load textures
        auto& am = AssetManager::Get();
        texPlayer  = am.CreateTexture("assets/images/player.png");
        texEnemy1  = am.CreateTexture("assets/images/enemy1.png");
        texEnemy2  = am.CreateTexture("assets/images/enemy2.png");
        texEnemy3  = am.CreateTexture("assets/images/enemy3.png");
        texBullet  = am.CreateTexture("assets/images/bullet.png");
        texCircle  = am.CreateTexture("assets/images/circle-bullet.png");
        texOrbit   = am.CreateTexture("assets/images/orbit-bullet.png");
        texEBullet = am.CreateTexture("assets/images/enemy-bullet.png");
        texECircle = am.CreateTexture("assets/images/enemy-circle-bullet.png");
        texBg[0]   = am.CreateTexture("assets/images/nightsky-bg.png");
        texBg[1]   = am.CreateTexture("assets/images/nightsky-mountains.png");
        texBg[2]   = am.CreateTexture("assets/images/nightsky-fg.png");
        texTitle   = am.CreateTexture("assets/images/title.png");

        fontID = GraphicsServer::Get()->LoadFont("assets/fonts/NotoSans-SemiBold.ttf", 22.0f);

        bgm     = audio.LoadMusic("assets/sounds/sky-lines.ogg");
        sfxExp  = audio.LoadSound("assets/sounds/explosion.wav");
        sfxOver = audio.LoadSound("assets/sounds/game-over.wav");

        // Parallax background tiles
        for (int i = 0; i < 3; i++) {
            auto makeTile = [&](float startX) -> GameObject* {
                auto* obj = CreateGameObject(glm::vec2(startX, 0.0f));
                obj->AddComponent<SpriteComponent>(SpriteProps{
                    .size      = glm::vec2(WORLD, WORLD),
                    .pivot     = glm::vec2(0.0f, 0.0f),
                    .color     = glm::vec4(1,1,1,1),
                    .textureID = (int)texBg[i],
                    .layer     = CanvasLayer::LAYER_WORLD_2D,
                    .flipY     = true,
                    .zOrder    = i,
                });
                return obj;
            };
            bg[i].a = makeTile(0.0f);
            bg[i].b = makeTile(WORLD);
        }

        bgTime = 0.0f;
        enterTitle();
    }

    void OnUpdate(float dt, float /*time*/) override {
        bgTime += dt;
        updateParallax();

        switch (state) {
        case GameState::Title:    updateTitle(dt);    break;
        case GameState::Playing:  updatePlaying(dt);  break;
        case GameState::GameOver: updateGameOver(dt); break;
        }

        if (input.IsKeyDown(Key::ESCAPE)) Quit();
    }

    // ───────────────── Parallax ───────────────
    void updateParallax() {
        for (int i = 0; i < 3; i++) {
            // Scroll left; fmod keeps value in (-600, 0]
            float xA = std::fmod(-BG_SPEED[i] * bgTime, WORLD);
            if (xA > 0.0f) xA -= WORLD; // normalise to (-600, 0]
            if (bg[i].a) bg[i].a->SetPosition(glm::vec3(xA,          0.0f, 0.0f));
            if (bg[i].b) bg[i].b->SetPosition(glm::vec3(xA + WORLD,  0.0f, 0.0f));
        }
    }

    // ───────────────── Title ───────────────
    void enterTitle() {
        state = GameState::Title;
        titleObj = CreateGameObject(glm::vec2(HALF, HALF));
        titleObj->AddComponent<SpriteComponent>(SpriteProps{
            .size      = glm::vec2(WORLD, WORLD),
            .pivot     = glm::vec2(0.5f, 0.5f),
            .color     = glm::vec4(1,1,1,1),
            .textureID = (int)texTitle,
            .layer     = CanvasLayer::LAYER_WORLD_2D,
            .flipY     = true,
            .zOrder    = 20,
        });
    }

    void updateTitle(float /*dt*/) {
        auto* gs = GraphicsServer::Get();
        gs->DrawText(fontID, "Press SPACE or ENTER to start",
            HALF - 140.0f, WORLD - 60.0f, 0.8f, glm::vec4(1.0f,1.0f,0.0f,1.0f));

        if (input.IsKeyPressed(Key::SPACE) || input.IsKeyPressed(Key::ENTER)) {
            if (titleObj) { titleObj->SetActive(false); titleObj = nullptr; }
            startGame();
        }
    }

    // ───────────────── Game ───────────────
    void startGame() {
        // Clean up any objects from a previous session
        for (auto& b : bullets) if (b.obj) b.obj->SetActive(false);
        for (auto& e : enemies) if (e.obj) e.obj->SetActive(false);
        if (playerObj) { playerObj->SetActive(false); playerObj = nullptr; }
        bullets.clear();
        enemies.clear();

        // Reset game state
        score = 0.0; kills = 0; plLevel = 1; plXP = 0.0f; nextXP = 100.0f;
        fireCooldown = 0.5f; fireTimer = 0.0f;
        waveTimer = 0.0f; waveCount = 0; enemySysFireTimer = 0.0f;
        plX = toEngX(-264.0f); plY = toEngY(-16.0f);

        // Spawn player sprite
        playerObj = CreateGameObject(glm::vec2(plX, plY));
        playerObj->AddComponent<SpriteComponent>(SpriteProps{
            .size      = glm::vec2(PL_W, PL_H),
            .pivot     = glm::vec2(0.5f, 0.5f),
            .color     = glm::vec4(1,1,1,1),
            .textureID = (int)texPlayer,
            .layer     = CanvasLayer::LAYER_WORLD_2D,
            .flipY     = true,
            .zOrder    = 5,
        });

        spawnWave();
        audio.PlayMusic(bgm);
        state = GameState::Playing;
    }

    void updatePlaying(float dt) {
        score += dt * 100.0; // 100 pts/s (original: dt_ms * 0.1)

        handleInput(dt);

        waveTimer += dt;
        if (waveTimer >= 10.0f) { waveTimer = 0.0f; spawnWave(); }

        enemySysFireTimer -= dt;
        for (auto& e : enemies) if (e.alive) updateEnemy(e, dt);
        for (auto& b : bullets) if (b.alive) updateBullet(b, dt);

        checkCollisions();
        flushDead();
        drawHUD();
    }

    void handleInput(float dt) {
        float dx = 0.0f, dy = 0.0f;
        if (input.IsKeyDown(Key::LEFT))  dx = -500.0f * dt;
        if (input.IsKeyDown(Key::RIGHT)) dx = +500.0f * dt;
        if (input.IsKeyDown(Key::UP))    dy = -500.0f * dt; // screen-space: UP = -y
        if (input.IsKeyDown(Key::DOWN))  dy = +500.0f * dt;

        plX = std::clamp(plX + dx, PL_W * 0.5f, WORLD - PL_W * 0.5f);
        plY = std::clamp(plY + dy, PL_H * 0.5f, WORLD - PL_H * 0.5f);
        playerObj->SetPosition(glm::vec3(plX, plY, 0.0f));

        fireTimer -= dt;
        if (fireTimer <= 0.0f) {
            fireTimer = fireCooldown;
            spawnPlayerBullets();
        }
    }

    void spawnPlayerBullets() {
        float bx = plX + PL_W * 0.5f; // right edge of player
        float by = plY;
        spawnBullet(0, bx, by);
        spawnBullet(1, bx, by);
        spawnBullet(2, bx, by);
    }

    void spawnBullet(int type, float x, float y) {
        float maxLife = 99999.0f;
        if (type == 2)  maxLife = 2.0f;  // orbit: 2000ms
        if (type == -3) maxLife = 2.5f;  // spiral: 2500ms

        GLuint tex = texBullet;
        switch (type) {
        case 1:  tex = texCircle;  break;
        case 2:  tex = texOrbit;   break;
        case -1: tex = texECircle; break;
        case -2: tex = texEBullet; break;
        case -3: tex = texECircle; break;
        }

        auto* obj = CreateGameObject(glm::vec2(x, y));
        obj->AddComponent<SpriteComponent>(SpriteProps{
            .size      = glm::vec2(8.0f, 8.0f),
            .pivot     = glm::vec2(0.5f, 0.5f),
            .color     = glm::vec4(1,1,1,1),
            .textureID = (int)tex,
            .layer     = CanvasLayer::LAYER_WORLD_2D,
            .flipY     = true,
            .zOrder    = 3,
        });
        bullets.push_back({ obj, type, x, y, 0.0f, maxLife, true });
    }

    void updateBullet(BulletData& b, float dt) {
        b.lifetime += dt;
        if (b.lifetime >= b.maxLife) { b.alive = false; return; }

        const float lt = b.lifetime;
        switch (b.type) {
        case 0:  // Player normal -- straight right
            b.x += 800.0f * dt;
            break;
        case 1:  // Player circle -- fast + random y jitter
            b.x += 1200.0f * dt;
            b.y += (rnd() * 2.0f - 1.0f) * 300.0f * dt;
            break;
        case 2: { // Player orbit -- sinusoidal spiral
            // Original: dx = 10*sin(0.02*ltMs) + 0.2*dt, dy = 10*-cos(0.02*ltMs)
            // Engine: orbit speed 20 rad/s (0.02 rad/ms x 1000)
            b.x += (10.0f * std::sin(20.0f * lt) + 200.0f) * dt;
            b.y += 300.0f * -std::cos(20.0f * lt) * dt;
            break;
        }
        case -1: case -2: // Enemy straight -- moves left
            b.x -= 600.0f * dt;
            break;
        case -3: { // Enemy spiral -- expanding outward
            // Original (unscaled by dt, ~60fps): dx = lt_ms*0.002*cos(0.005*lt_ms)
            // Approximated with dt compensation:
            const float ltMs = lt * 1000.0f;
            b.x += ltMs * 0.002f * std::cos(5.0f * lt) * 60.0f * dt;
            b.y += ltMs * 0.002f * std::sin(5.0f * lt) * 60.0f * dt;
            break;
        }
        }

        // Despawn when off-screen
        if (b.x > WORLD + 50.0f || b.x < -50.0f ||
            b.y < -50.0f       || b.y > WORLD + 50.0f) {
            b.alive = false;
            return;
        }
        b.obj->SetPosition(glm::vec3(b.x, b.y, 0.0f));
    }

    void updateEnemy(EnemyData& e, float dt) {
        // Horizontal movement
        e.x -= e.speed * dt;

        // Vertical raid movement
        e.raidStartTimer -= dt;
        if (e.raidStartTimer <= 0.0f && e.raidEndTimer > 0.0f && e.raidDuration > 0.0f) {
            e.y += (e.raidDistance / e.raidDuration) * dt;
            e.raidEndTimer -= dt;
        }

        // Firing
        e.fireTimer -= dt;
        if (e.fireTimer <= 0.0f) {
            bool sysOk = (enemySysFireTimer <= 0.0f && rnd() < 0.5f);
            if (e.type == 3 || sysOk) {
                int bt = (e.type == 1) ? -1 : (e.type == 2 ? -2 : -3);
                spawnBullet(bt, e.x - 16.0f, e.y);
                e.fireTimer      = e.fireCooldown;
                enemySysFireTimer = 0.5f;
            }
        }

        // Remove when fully off left edge
        if (e.x < -48.0f) { e.alive = false; return; }
        e.obj->SetPosition(glm::vec3(e.x, e.y, 0.0f));
    }

    void checkCollisions() {
        for (auto& b : bullets) {
            if (!b.alive || b.type < 0) continue;
            for (auto& e : enemies) {
                if (!e.alive) continue;
                if (aabb(b.x, b.y, 8.0f, 8.0f, e.x, e.y, 32.0f, 32.0f)) {
                    b.alive = false;
                    killEnemy(e);
                }
            }
        }
        // Note: player death is intentionally disabled (isInvincible = true in original).
        // Uncomment the block below to enable it:
        //
        // for (auto& b : bullets) {
        //     if (!b.alive || b.type >= 0) continue;
        //     if (aabb(b.x, b.y, 8, 8, plX, plY, PL_W, PL_H)) {
        //         b.alive = false;
        //         triggerGameOver();
        //     }
        // }
    }

    void killEnemy(EnemyData& e) {
        e.alive = false;
        score  += 100.0;
        kills++;
        plXP += 5.0f * e.level + 5.0f;
        audio.PlaySoundVariation(sfxExp, 0.1f, 0.05f);

        while (plXP >= nextXP) {
            plXP -= nextXP;
            plLevel++;
            nextXP       = 50.0f * plLevel * plLevel + 50.0f;
            fireCooldown = std::max(0.55f - plLevel * 0.05f, 0.05f);
        }
    }

    void spawnWave() {
        waveCount++;
        // Matches original off-by-one: floor(random*(24-1)) -> 0..22
        int idx  = (int)(rnd() * 23);
        float ox = (rnd() - 0.5f) * 30.0f;
        float oy = (rnd() - 0.5f) * 30.0f;

        for (int row = 0; row < 5; row++) {
            for (int col = 0; col < 8; col++) {
                int cell = WAVES[idx][row][col];
                if (cell == 0) continue;
                // Original grid position (center origin, y-down)
                float origX = col * 90.0f - 300.0f + ox;
                float origY = row * 90.0f - 300.0f + 100.0f + oy;
                // Screen-space position; spawn off right edge
                float ex = toEngX(origX) + WORLD;
                float ey = toEngY(origY);
                spawnEnemy(cell, ex, ey, waveCount);
            }
        }
    }

    void spawnEnemy(int type, float x, float y, int lvl) {
        // Speed: original 0.05*(floor(lvl/5)+1) px/ms x 1000 = px/s, capped at 500
        float spd     = std::min(std::floor(lvl / 5.0f + 1.0f) * 50.0f, 500.0f);
        float fireCD  = std::max(2.0f - 0.1f * lvl, 0.5f);
        float raidDur = (type == 1) ? 0.0f : (type == 2 ? 1.0f : 2.0f);
        // Raid direction: move toward vertical center
        float raidDist = (y < HALF ? +1.0f : -1.0f) * 600.0f * rnd();
        // Raid start delay: original (1000/speed_px_ms)*rnd() ms
        //                   engine: (1000/spd)*rnd() seconds
        float raidStart = (raidDur > 0.0f) ? (1000.0f / spd) * rnd() : 0.0f;

        GLuint tex = (type == 1) ? texEnemy1 : (type == 2 ? texEnemy2 : texEnemy3);
        auto* obj = CreateGameObject(glm::vec2(x, y));
        obj->AddComponent<SpriteComponent>(SpriteProps{
            .size      = glm::vec2(32.0f, 32.0f),
            .pivot     = glm::vec2(0.5f, 0.5f),
            .color     = glm::vec4(1,1,1,1),
            .textureID = (int)tex,
            .layer     = CanvasLayer::LAYER_WORLD_2D,
            .flipY     = true,
            .zOrder    = 4,
        });

        enemies.push_back({
            obj, type, lvl, x, y,
            spd, fireCD, fireCD * rnd(),
            raidDist, raidStart, 1.0f, raidDur,
            true
        });
    }

    void flushDead() {
        for (auto& b : bullets)
            if (!b.alive && b.obj) { b.obj->SetActive(false); b.obj = nullptr; }
        bullets.erase(
            std::remove_if(bullets.begin(), bullets.end(),
                [](const BulletData& b){ return !b.alive; }),
            bullets.end());

        for (auto& e : enemies)
            if (!e.alive && e.obj) { e.obj->SetActive(false); e.obj = nullptr; }
        enemies.erase(
            std::remove_if(enemies.begin(), enemies.end(),
                [](const EnemyData& e){ return !e.alive; }),
            enemies.end());
    }

    void drawHUD() {
        auto* gs = GraphicsServer::Get();
        std::ostringstream oss;
        oss << "Score: " << std::setw(8) << std::setfill('0') << (long long)score
            << "  Kills: " << std::setw(4) << std::setfill('0') << kills
            << "  Lv." << plLevel
            << " (" << std::fixed << std::setprecision(1)
            << (plXP / nextXP * 100.0f) << "%)";
        gs->DrawText(fontID, oss.str(), 10.0f, 10.0f, 0.8f, glm::vec4(1.0f,1.0f,0.0f,1.0f));
    }

    // ───────────────── Game Over ───────────────
    void triggerGameOver() {
        if ((long long)score > hiScore) hiScore = (long long)score;
        audio.StopMusic(bgm);
        audio.PlaySound(sfxOver);
        state = GameState::GameOver;
    }

    void updateGameOver(float /*dt*/) {
        auto* gs = GraphicsServer::Get();
        gs->DrawText(fontID, "GAME  OVER",
            HALF - 80.0f, WORLD * 0.30f, 1.5f, glm::vec4(1.0f,0.2f,0.2f,1.0f));
        gs->DrawText(fontID, "Score: " + std::to_string((long long)score),
            HALF - 80.0f, WORLD * 0.45f, 1.0f, glm::vec4(1,1,1,1));
        gs->DrawText(fontID, "Best:  " + std::to_string(hiScore),
            HALF - 80.0f, WORLD * 0.52f, 1.0f, glm::vec4(1.0f,1.0f,0.0f,1.0f));
        gs->DrawText(fontID, "Press SPACE or ENTER to play again",
            HALF - 150.0f, WORLD * 0.65f, 0.8f, glm::vec4(0.8f,0.8f,0.8f,1.0f));

        if (input.IsKeyPressed(Key::SPACE) || input.IsKeyPressed(Key::ENTER)) {
            startGame();
        }
    }

    // ───────────────── Helpers ───────────────
    static bool aabb(float ax, float ay, float aw, float ah,
                     float bx, float by, float bw, float bh) {
        return std::abs(ax - bx) < (aw + bw) * 0.5f &&
               std::abs(ay - by) < (ah + bh) * 0.5f;
    }
};

#ifdef __EMSCRIPTEN__
static const std::vector<std::string> kAssets = {
    "assets/textures/default_diff.ktx2",
    "assets/textures/default_norm.ktx2",
    "assets/textures/default_ao.ktx2",
    "assets/textures/default_rough.ktx2",
    "assets/textures/default_metallic.ktx2",
    "assets/images/player.ktx2",
    "assets/images/enemy1.ktx2",
    "assets/images/enemy2.ktx2",
    "assets/images/enemy3.ktx2",
    "assets/images/bullet.ktx2",
    "assets/images/circle-bullet.ktx2",
    "assets/images/orbit-bullet.ktx2",
    "assets/images/enemy-bullet.ktx2",
    "assets/images/enemy-circle-bullet.ktx2",
    "assets/images/nightsky-bg.ktx2",
    "assets/images/nightsky-mountains.ktx2",
    "assets/images/nightsky-fg.ktx2",
    "assets/images/title.ktx2",
    "assets/sounds/sky-lines.ogg",
    "assets/sounds/explosion.wav",
    "assets/sounds/game-over.wav",
};

static void StartGame();

int main(int argc, char* argv[]) {
    FileSystem::Get().Prefetch(kAssets, StartGame);
    return 0;
}

static void StartGame() {
    static MidnightSkyraiders game({
        .windowTitle        = "Midnight Skyraiders",
        .windowWidth        = 600,
        .windowHeight       = 600,
        .enablePhysics3D    = false,
        .useDefaultTextures = true,
        .useDefaultShaders  = true,
    });
    game.Run();
}
#else
int main(int argc, char* argv[]) {
    MidnightSkyraiders game({
        .windowTitle        = "Midnight Skyraiders",
        .windowWidth        = 600,
        .windowHeight       = 600,
        .enablePhysics3D    = false,
        .useDefaultTextures = true,
        .useDefaultShaders  = true,
    });
    game.Run();
    return 0;
}
#endif

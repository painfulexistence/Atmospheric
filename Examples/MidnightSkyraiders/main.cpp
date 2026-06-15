#include "Atmospheric.hpp"

#include <vector>
#include <algorithm>
#include <cmath>
#include <random>
#include <sstream>
#include <iomanip>
#include <string>
#include <functional>

// ─── Local components ─────────────────────────────────────────────────────────

// A single horizontally-scrolling background layer. Spawns two seamless tiles
// on attach and scrolls them left at `speed` px/s, wrapping continuously.
class ParallaxLayerComponent : public Component {
    int _textureID; float _worldSize, _speed; int _zOrder;
    GameObject* _tiles[2] = {};
    float _t = 0;
public:
    ParallaxLayerComponent(GameObject* go, int textureID, float worldSize, float speed, int zOrder)
        : _textureID(textureID), _worldSize(worldSize), _speed(speed), _zOrder(zOrder) {
        gameObject = go;
    }
    std::string GetName() const override { return "ParallaxLayerComponent"; }
    void OnAttach() override {
        auto* app = gameObject->GetApp();
        for (int i = 0; i < 2; i++) {
            _tiles[i] = app->CreateGameObject(glm::vec2(i * _worldSize, 0.0f));
            _tiles[i]->AddComponent<SpriteComponent>(SpriteProps{
                .size = glm::vec2(_worldSize, _worldSize),
                .pivot = glm::vec2(0.0f, 0.0f),
                .color = glm::vec4(1.0f),
                .textureID = _textureID,
                .layer = CanvasLayer::LAYER_WORLD_2D,
                .flipY = true,
                .zOrder = _zOrder,
            });
        }
    }
    void OnTick(float dt) override {
        _t += dt;
        float xA = std::fmod(-_speed * _t, _worldSize);
        if (xA > 0.0f) xA -= _worldSize;
        if (_tiles[0]) _tiles[0]->SetPosition(glm::vec3(xA, 0.0f, 0.0f));
        if (_tiles[1]) _tiles[1]->SetPosition(glm::vec3(xA + _worldSize, 0.0f, 0.0f));
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Wave data: 24 waves × 5 rows × 8 cols  (0=empty, 1-3=enemy type)
// ─────────────────────────────────────────────────────────────────────────────
static const int WAVES[24][5][8] = {
    {{0,1,1,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,1,1,0,0,0,0,0}},
    {{0,0,1,0,0,0,0,0},{0,1,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,1,0,0,0,0,0,0},{0,0,1,0,0,0,0,0}},
    {{0,1,0,1,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,1,0,1,0,0,0,0}},
    {{0,0,1,0,0,0,0,0},{0,1,0,0,0,0,0,0},{1,0,0,0,0,0,0,0},{0,1,0,0,0,0,0,0},{0,0,1,0,0,0,0,0}},
    {{0,0,1,0,0,0,0,0},{0,0,1,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,1,0,0,0,0,0},{0,0,1,0,0,0,0,0}},
    {{1,0,0,0,0,0,0,0},{0,1,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,1,0,0,0,0,0,0},{0,0,1,0,0,0,0,0}},
    {{0,0,1,0,0,0,0,0},{0,1,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,1,0,0,0,0,0,0},{1,0,0,0,0,0,0,0}},
    {{0,0,0,0,0,0,0,0},{1,0,1,0,0,0,0,0},{0,0,0,0,0,0,0,0},{1,0,1,0,0,0,0,0},{0,0,0,0,0,0,0,0}},
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

// World is 600×600 screen-space; original game uses center origin, y-down.
// Conversion: eng_x = orig_x + 300, eng_y = orig_y + 300.
static constexpr float WORLD = 600.0f;
static constexpr float HALF  = WORLD * 0.5f;

static float toEngX(float ox) { return ox + HALF; }
static float toEngY(float oy) { return oy + HALF; }

enum class GameState { Title, Playing, GameOver };

// ─────────────────────────────────────────────────────────────────────────────
// Game-specific components
//
// These are defined here rather than in ExampleComponents because they are
// tightly coupled to this game's coordinate system and bullet-type enum.
// ─────────────────────────────────────────────────────────────────────────────

// Moves a bullet each tick according to its type; self-deactivates when done.
// Negative types are enemy bullets; non-negative are player bullets.
class BulletMovementComponent : public Component {
    int   _type;
    float _bx, _by;
    float _lifetime = 0.0f, _maxLife;
public:
    BulletMovementComponent(GameObject* go, int type, float x, float y, float maxLife)
        : _type(type), _bx(x), _by(y), _maxLife(maxLife) {}

    std::string GetName() const override { return "BulletMovementComponent"; }
    int GetType() const { return _type; }

    void OnTick(float dt) override {
        _lifetime += dt;
        if (_lifetime >= _maxLife) { gameObject->SetActive(false); return; }

        const float lt = _lifetime;
        switch (_type) {
        case 0:  // player normal — straight right
            _bx += 800.0f * dt;
            break;
        case 1:  // player circle — fast + random y jitter
            _bx += 1200.0f * dt;
            _by += (rnd() * 2.0f - 1.0f) * 300.0f * dt;
            break;
        case 2: { // player orbit — sinusoidal spiral
            _bx += (10.0f * std::sin(20.0f * lt) + 200.0f) * dt;
            _by += 300.0f * -std::cos(20.0f * lt) * dt;
            break;
        }
        case -1: case -2:  // enemy straight — moves left
            _bx -= 600.0f * dt;
            break;
        case -3: { // enemy spiral
            const float ltMs = lt * 1000.0f;
            _bx += ltMs * 0.002f * std::cos(5.0f * lt) * 60.0f * dt;
            _by += ltMs * 0.002f * std::sin(5.0f * lt) * 60.0f * dt;
            break;
        }
        }

        if (_bx > WORLD + 50.0f || _bx < -50.0f ||
            _by < -50.0f        || _by > WORLD + 50.0f) {
            gameObject->SetActive(false);
            return;
        }
        gameObject->SetPosition(glm::vec3(_bx, _by, 0.0f));
    }
};

// Drives an enemy's horizontal scroll + optional vertical raid each tick.
// Self-deactivates when the enemy scrolls off the left edge.
class EnemyMovementComponent : public Component {
    int   _type, _level;
    float _speed;
    float _raidDist, _raidStart, _raidEnd, _raidDur;
public:
    EnemyMovementComponent(GameObject* go, int type, int level, float speed,
                           float raidDist, float raidStart, float raidDur)
        : _type(type), _level(level), _speed(speed),
          _raidDist(raidDist), _raidStart(raidStart), _raidEnd(raidDur), _raidDur(raidDur) {}

    std::string GetName() const override { return "EnemyMovementComponent"; }
    int GetLevel()     const { return _level; }
    int GetEnemyType() const { return _type; }

    void OnTick(float dt) override {
        glm::vec3 pos = gameObject->GetPosition();
        pos.x -= _speed * dt;

        _raidStart -= dt;
        if (_raidStart <= 0.0f && _raidEnd > 0.0f && _raidDur > 0.0f) {
            pos.y += (_raidDist / _raidDur) * dt;
            _raidEnd -= dt;
        }

        if (pos.x < -48.0f) { gameObject->SetActive(false); return; }
        gameObject->SetPosition(pos);
    }
};

// Counts down a fire timer and invokes onFire when ready. The callback uses
// DeferSpawn to create the bullet object safely outside the tick loop.
class EnemyFireComponent : public Component {
    int   _type;
    float _fireCD, _fireTimer;
    float* _sysFireTimer;
    std::function<void(int, float, float)> _onFire;
public:
    EnemyFireComponent(GameObject* go, int type, float fireCD, float initTimer,
                       float* sysFireTimer,
                       std::function<void(int, float, float)> onFire)
        : _type(type), _fireCD(fireCD), _fireTimer(initTimer),
          _sysFireTimer(sysFireTimer), _onFire(std::move(onFire)) {}

    std::string GetName() const override { return "EnemyFireComponent"; }

    void OnTick(float dt) override {
        _fireTimer -= dt;
        if (_fireTimer > 0.0f) return;

        bool sysOk = (*_sysFireTimer <= 0.0f && rnd() < 0.5f);
        if (_type == 3 || sysOk) {
            glm::vec3 pos = gameObject->GetPosition();
            int bt = (_type == 1) ? -1 : (_type == 2 ? -2 : -3);
            if (_onFire) _onFire(bt, pos.x - 16.0f, pos.y);
            _fireTimer    = _fireCD;
            *_sysFireTimer = 0.5f;
        }
    }
};

// Holds the player's progression state (level / XP / fire cooldown). Lives on
// the player GameObject so it is selectable and tunable in the editor.
// killEnemy feeds XP in; PlayerInputComponent reads the cooldown out.
class PlayerComponent : public Component {
    int   _level   = 1;
    float _xp      = 0.0f;
    float _nextXP  = 100.0f;
    float _fireCD  = 0.5f;
public:
    PlayerComponent(GameObject* go) {}

    std::string GetName() const override { return "PlayerComponent"; }

    int   Level()        const { return _level; }
    float XP()           const { return _xp; }
    float NextXP()       const { return _nextXP; }
    float FireCooldown() const { return _fireCD; }

    void Reset() {
        _level = 1; _xp = 0.0f; _nextXP = 100.0f; _fireCD = 0.5f;
    }

    // Award XP for a kill of the given enemy level and apply level-ups.
    void AddXP(int enemyLevel) {
        _xp += 5.0f * enemyLevel + 5.0f;
        while (_xp >= _nextXP) {
            _xp -= _nextXP;
            _level++;
            _nextXP = 50.0f * _level * _level + 50.0f;
            _fireCD = std::max(0.55f - _level * 0.05f, 0.05f);
        }
    }

    void DrawImGui() override {
        if (ImGui::CollapsingHeader("PlayerComponent", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Level: %d", _level);
            ImGui::Text("XP: %.0f / %.0f", _xp, _nextXP);
            ImGui::DragFloat("Fire cooldown", &_fireCD, 0.01f, 0.02f, 1.0f);
        }
    }
};

// Handles WASD player movement and triggers auto-fire via onFire callback.
// Reads the fire cooldown from PlayerComponent so level-up changes apply at once.
class PlayerInputComponent : public Component {
    float  _speed = 500.0f;
    float  _plW, _plH;
    PlayerComponent* _player;
    float  _fireTimer;
    std::function<void(float, float)> _onFire;
public:
    PlayerInputComponent(GameObject* go, PlayerComponent* player, float w, float h,
                         std::function<void(float, float)> onFire)
        : _plW(w), _plH(h), _player(player), _fireTimer(player->FireCooldown()),
          _onFire(std::move(onFire)) {}

    std::string GetName() const override { return "PlayerInputComponent"; }

    void OnTick(float dt) override {
        auto* inp = gameObject->GetApp()->GetInput();
        float dx = 0, dy = 0;
        if (inp->IsKeyDown(Key::LEFT))  dx = -_speed * dt;
        if (inp->IsKeyDown(Key::RIGHT)) dx = +_speed * dt;
        if (inp->IsKeyDown(Key::UP))    dy = -_speed * dt;
        if (inp->IsKeyDown(Key::DOWN))  dy = +_speed * dt;

        glm::vec3 pos = gameObject->GetPosition();
        pos.x = std::clamp(pos.x + dx, _plW * 0.5f, WORLD - _plW * 0.5f);
        pos.y = std::clamp(pos.y + dy, _plH * 0.5f, WORLD - _plH * 0.5f);
        gameObject->SetPosition(pos);

        _fireTimer -= dt;
        if (_fireTimer <= 0.0f) {
            _fireTimer = _player->FireCooldown();
            if (_onFire) _onFire(pos.x, pos.y);
        }
    }

    void DrawImGui() override {
        if (ImGui::CollapsingHeader("PlayerInputComponent")) {
            ImGui::DragFloat("Move speed", &_speed, 5.0f, 50.0f, 1500.0f);
        }
    }
};

// World-level director: owns score / wave / system-fire state for the whole
// match. Attached to a "GameWorld" GameObject so all of this is visible and
// tunable in the editor. Its OnTick advances score, the wave timer, and the
// shared enemy-fire throttle; spawning a wave is delegated via onSpawnWave.
class GameDirectorComponent : public Component {
    double _score = 0.0;
    int    _kills = 0;
    int    _waveCount = 0;
    float  _waveTimer = 0.0f;
    float  _waveInterval = 10.0f;       // tunable
    float  _scoreRate = 100.0f;         // points/sec, tunable
    float  _enemySysFireTimer = 0.0f;
    bool   _running = false;
    std::function<void()> _onSpawnWave;
public:
    GameDirectorComponent(GameObject* go, std::function<void()> onSpawnWave)
        : _onSpawnWave(std::move(onSpawnWave)) {}

    std::string GetName() const override { return "GameDirectorComponent"; }

    void Reset() {
        _score = 0.0; _kills = 0; _waveCount = 0;
        _waveTimer = 0.0f; _enemySysFireTimer = 0.0f;
    }
    void SetRunning(bool r) { _running = r; }

    double Score()     const { return _score; }
    int    Kills()     const { return _kills; }
    int    WaveCount() const { return _waveCount; }
    float* SysFireTimer()    { return &_enemySysFireTimer; }

    void AddScore(double s) { _score += s; }
    void AddKill()          { _kills++; }
    void OnWaveSpawned()    { _waveCount++; }

    void OnTick(float dt) override {
        if (!_running) return;
        _score += dt * _scoreRate;
        _enemySysFireTimer -= dt;
        _waveTimer += dt;
        if (_waveTimer >= _waveInterval) {
            _waveTimer = 0.0f;
            if (_onSpawnWave) _onSpawnWave();
        }
    }

    void DrawImGui() override {
        if (ImGui::CollapsingHeader("GameDirectorComponent", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Score: %lld", (long long)_score);
            ImGui::Text("Kills: %d", _kills);
            ImGui::Text("Wave:  %d", _waveCount);
            ImGui::Text("Next wave in: %.1fs", std::max(0.0f, _waveInterval - _waveTimer));
            ImGui::Separator();
            ImGui::DragFloat("Wave interval", &_waveInterval, 0.5f, 1.0f, 60.0f);
            ImGui::DragFloat("Score rate", &_scoreRate, 5.0f, 0.0f, 1000.0f);
            if (ImGui::Button("Spawn wave now")) {
                _waveTimer = 0.0f;
                if (_onSpawnWave) _onSpawnWave();
            }
        }
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Application
// ─────────────────────────────────────────────────────────────────────────────

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

    MusicID bgm     = 0;
    SoundID sfxExp  = 0;
    SoundID sfxOver = 0;

    static constexpr float BG_SPEED[3] = { 5.0f, 40.0f, 120.0f };

    // Title
    GameObject* titleObj  = nullptr;

    // Player — progression state lives in PlayerComponent (editor-inspectable)
    GameObject*      playerObj = nullptr;
    PlayerComponent* player    = nullptr;
    static constexpr float PL_W = 32.0f, PL_H = 32.0f;

    // World director — score / waves / system-fire throttle (editor-inspectable)
    GameObject*            worldObj = nullptr;
    GameDirectorComponent* director = nullptr;

    // Entity tracking for collision + cleanup (behavior lives in components)
    std::vector<GameObject*> _bullets;
    std::vector<GameObject*> _enemies;

    long long hiScore = 0;  // persists across sessions, so kept app-side

    // ── lifecycle ──────────────────────────────────────────────────────────────

    void OnInit() override {
        GoScene("main", [this]{ OnLoad(); });
    }

    void OnLoad() override {
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

        auto* background = CreateGameObject();
        background->SetName("Background");
        for (int i = 0; i < 3; i++) {
            background->AddComponent<ParallaxLayerComponent>((int)texBg[i], WORLD, BG_SPEED[i], i);
        }

        // World director: a single "GameWorld" entity owns match-wide state.
        // Wave spawns are deferred because the director ticks inside the loop.
        worldObj = CreateGameObject();
        worldObj->SetName("GameWorld");
        director = static_cast<GameDirectorComponent*>(
            worldObj->AddComponent<GameDirectorComponent>(
                [this]{ DeferSpawn([this]{ spawnWave(); }); }
            )
        );

        enterTitle();
    }

    void OnUpdate(float dt, float /*time*/) override {
        switch (state) {
        case GameState::Title:    updateTitle(dt);    break;
        case GameState::Playing:  updatePlaying(dt);  break;
        case GameState::GameOver: updateGameOver(dt); break;
        }
        if (input.IsKeyDown(Key::ESCAPE)) Quit();
    }

    // ── title ─────────────────────────────────────────────────────────────────

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
        GraphicsServer::Get()->DrawText(fontID,
            "Press SPACE or ENTER to start",
            HALF - 140.0f, WORLD - 60.0f, 0.8f, glm::vec4(1,1,0,1));

        if (input.IsKeyPressed(Key::SPACE) || input.IsKeyPressed(Key::ENTER)) {
            if (titleObj) { titleObj->SetActive(false); titleObj = nullptr; }
            startGame();
        }
    }

    // ── game ──────────────────────────────────────────────────────────────────

    void startGame() {
        for (auto* b : _bullets) b->SetActive(false);
        for (auto* e : _enemies) e->SetActive(false);
        if (playerObj) { playerObj->SetActive(false); playerObj = nullptr; }
        _bullets.clear();
        _enemies.clear();

        director->Reset();

        playerObj = CreateGameObject(glm::vec2(toEngX(-264.0f), toEngY(-16.0f)));
        playerObj->SetName("Player");
        playerObj->AddComponent<SpriteComponent>(SpriteProps{
            .size      = glm::vec2(PL_W, PL_H),
            .pivot     = glm::vec2(0.5f, 0.5f),
            .color     = glm::vec4(1,1,1,1),
            .textureID = (int)texPlayer,
            .layer     = CanvasLayer::LAYER_WORLD_2D,
            .flipY     = true,
            .zOrder    = 5,
        });
        player = static_cast<PlayerComponent*>(playerObj->AddComponent<PlayerComponent>());
        playerObj->AddComponent<PlayerInputComponent>(
            player, PL_W, PL_H,
            [this](float px, float py) { spawnPlayerBulletsDeferred(px, py); }
        );

        spawnWave();
        audio.PlayMusic(bgm);
        director->SetRunning(true);
        state = GameState::Playing;
    }

    // OnUpdate runs before the entity tick loop. Score/wave timing now live in
    // GameDirectorComponent::OnTick; this handles only cross-entity work.
    void updatePlaying(float /*dt*/) {
        checkCollisions();
        flushDead();
        drawHUD();
    }

    // ── spawn ─────────────────────────────────────────────────────────────────

    // Called from PlayerInputComponent::OnTick — must defer because we are
    // inside the entity tick loop and CreateGameObject would invalidate it.
    void spawnPlayerBulletsDeferred(float px, float py) {
        float bx = px + PL_W * 0.5f;
        DeferSpawn([this, bx, py]{ spawnBullet(0, bx, py); });
        DeferSpawn([this, bx, py]{ spawnBullet(1, bx, py); });
        DeferSpawn([this, bx, py]{ spawnBullet(2, bx, py); });
    }

    void spawnBullet(int type, float x, float y) {
        float maxLife = 99999.0f;
        if (type ==  2) maxLife = 2.0f;
        if (type == -3) maxLife = 2.5f;

        GLuint tex = texBullet;
        switch (type) {
        case  1: tex = texCircle;  break;
        case  2: tex = texOrbit;   break;
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
        obj->AddComponent<BulletMovementComponent>(type, x, y, maxLife);
        _bullets.push_back(obj);
    }

    void spawnWave() {
        director->OnWaveSpawned();
        int   wave = director->WaveCount();
        int   idx  = (int)(rnd() * 23);
        float ox   = (rnd() - 0.5f) * 30.0f;
        float oy   = (rnd() - 0.5f) * 30.0f;

        for (int row = 0; row < 5; row++) {
            for (int col = 0; col < 8; col++) {
                int cell = WAVES[idx][row][col];
                if (cell == 0) continue;
                float ex = toEngX(col * 90.0f - 300.0f + ox) + WORLD;
                float ey = toEngY(row * 90.0f - 300.0f + 100.0f + oy);
                spawnEnemy(cell, ex, ey, wave);
            }
        }
    }

    void spawnEnemy(int type, float x, float y, int lvl) {
        float spd      = std::min(std::floor(lvl / 5.0f + 1.0f) * 50.0f, 500.0f);
        float fireCD   = std::max(2.0f - 0.1f * lvl, 0.5f);
        float raidDur  = (type == 1) ? 0.0f : (type == 2 ? 1.0f : 2.0f);
        float raidDist = (y < HALF ? +1.0f : -1.0f) * 600.0f * rnd();
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
        obj->AddComponent<EnemyMovementComponent>(type, lvl, spd, raidDist, raidStart, raidDur);
        // Enemy bullets are deferred: EnemyFireComponent is inside the tick loop.
        obj->AddComponent<EnemyFireComponent>(
            type, fireCD, fireCD * rnd(),
            director->SysFireTimer(),
            [this](int bt, float fx, float fy) {
                DeferSpawn([this, bt, fx, fy]{ spawnBullet(bt, fx, fy); });
            }
        );
        _enemies.push_back(obj);
    }

    // ── collision + cleanup ────────────────────────────────────────────────────

    // Runs in OnUpdate (before entity ticks), so it sees positions from last frame.
    // At 60fps the one-frame lag is imperceptible.
    void checkCollisions() {
        for (auto* b : _bullets) {
            if (!b->isActive) continue;
            auto* bm = b->GetComponent<BulletMovementComponent>();
            if (!bm || bm->GetType() < 0) continue;  // skip enemy bullets

            glm::vec3 bpos = b->GetPosition();
            for (auto* e : _enemies) {
                if (!e->isActive) continue;
                glm::vec3 epos = e->GetPosition();
                if (aabb(bpos.x, bpos.y, 8, 8, epos.x, epos.y, 32, 32)) {
                    b->SetActive(false);
                    killEnemy(e);
                }
            }
        }
        // Player invincibility is preserved from the original (no friendly-fire check).
    }

    void killEnemy(GameObject* enemy) {
        enemy->SetActive(false);
        auto* em = enemy->GetComponent<EnemyMovementComponent>();
        int level = em ? em->GetLevel() : 1;

        director->AddScore(100.0);
        director->AddKill();
        if (player) player->AddXP(level);
        audio.PlaySoundVariation(sfxExp, 0.1f, 0.05f);
    }

    // Remove inactive entries from tracking vectors; the GameObjects themselves
    // remain in _entities (owned by Application) but will not tick.
    void flushDead() {
        auto inactive = [](GameObject* g) { return !g->isActive; };
        _bullets.erase(std::remove_if(_bullets.begin(), _bullets.end(), inactive), _bullets.end());
        _enemies.erase(std::remove_if(_enemies.begin(), _enemies.end(), inactive), _enemies.end());
    }

    // ── HUD ───────────────────────────────────────────────────────────────────

    void drawHUD() {
        auto* gs = GraphicsServer::Get();
        std::ostringstream oss;
        oss << "Score: " << std::setw(8) << std::setfill('0') << (long long)director->Score()
            << "  Kills: " << std::setw(4) << std::setfill('0') << director->Kills()
            << "  Lv." << player->Level()
            << " (" << std::fixed << std::setprecision(1)
            << (player->XP() / player->NextXP() * 100.0f) << "%)";
        gs->DrawText(fontID, oss.str(), 10.0f, 10.0f, 0.8f, glm::vec4(1,1,0,1));
    }

    // ── game over ─────────────────────────────────────────────────────────────

    void triggerGameOver() {
        director->SetRunning(false);
        if ((long long)director->Score() > hiScore) hiScore = (long long)director->Score();
        audio.StopMusic(bgm);
        audio.PlaySound(sfxOver);
        state = GameState::GameOver;
    }

    void updateGameOver(float /*dt*/) {
        auto* gs = GraphicsServer::Get();
        gs->DrawText(fontID, "GAME  OVER",
            HALF - 80.0f, WORLD * 0.30f, 1.5f, glm::vec4(1,0.2f,0.2f,1));
        gs->DrawText(fontID, "Score: " + std::to_string((long long)director->Score()),
            HALF - 80.0f, WORLD * 0.45f, 1.0f, glm::vec4(1,1,1,1));
        gs->DrawText(fontID, "Best:  " + std::to_string(hiScore),
            HALF - 80.0f, WORLD * 0.52f, 1.0f, glm::vec4(1,1,0,1));
        gs->DrawText(fontID, "Press SPACE or ENTER to play again",
            HALF - 150.0f, WORLD * 0.65f, 0.8f, glm::vec4(0.8f,0.8f,0.8f,1));
        if (input.IsKeyPressed(Key::SPACE) || input.IsKeyPressed(Key::ENTER)) {
            startGame();
        }
    }

    // ── helpers ───────────────────────────────────────────────────────────────

    static bool aabb(float ax, float ay, float aw, float ah,
                     float bx, float by, float bw, float bh) {
        return std::abs(ax - bx) < (aw + bw) * 0.5f &&
               std::abs(ay - by) < (ah + bh) * 0.5f;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Entry points
// ─────────────────────────────────────────────────────────────────────────────

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

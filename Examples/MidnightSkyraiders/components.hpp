#pragma once
#include "Atmospheric.hpp"
#include <cmath>
#include <algorithm>
#include <functional>
#include <random>
#include <sstream>
#include <iomanip>
#include <string>

static std::mt19937 g_rng{ std::random_device{}() };
static inline float rnd() {
    return std::uniform_real_distribution<float>(0.0f, 1.0f)(g_rng);
}

static constexpr float WORLD = 600.0f;
static constexpr float HALF  = WORLD * 0.5f;
static inline float toEngX(float ox) { return ox + HALF; }
static inline float toEngY(float oy) { return oy + HALF; }

// A single horizontally-scrolling background layer.
class ParallaxLayerComponent : public Component {
    TextureHandle _texture; float _worldSize, _speed; int _zOrder;
    GameObject* _tiles[2] = {};
    float _t = 0;
public:
    ParallaxLayerComponent(GameObject* go, TextureHandle texture, float worldSize, float speed, int zOrder)
        : _texture(texture), _worldSize(worldSize), _speed(speed), _zOrder(zOrder) {
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
                .texture = _texture,
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
        case 0:
            _bx += 800.0f * dt;
            break;
        case 1:
            _bx += 1200.0f * dt;
            _by += (rnd() * 2.0f - 1.0f) * 300.0f * dt;
            break;
        case 2: {
            _bx += (10.0f * std::sin(20.0f * lt) + 200.0f) * dt;
            _by += 300.0f * -std::cos(20.0f * lt) * dt;
            break;
        }
        case -1: case -2:
            _bx -= 600.0f * dt;
            break;
        case -3: {
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

// Counts down a fire timer and invokes onFire when ready.
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

// Player progression state (level / XP / fire cooldown).
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
        ImGui::Text("Level: %d", _level);
        ImGui::Text("XP: %.0f / %.0f", _xp, _nextXP);
        ImGui::DragFloat("Fire cooldown", &_fireCD, 0.01f, 0.02f, 1.0f);
    }
};

// Handles WASD movement and auto-fire. Reads fire cooldown from PlayerComponent.
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
        ImGui::DragFloat("Move speed", &_speed, 5.0f, 50.0f, 1500.0f);
    }
};

// World-level director: score / wave / system-fire state for the whole match.
class GameDirectorComponent : public Component {
    double _score = 0.0;
    int    _kills = 0;
    int    _waveCount = 0;
    float  _waveTimer = 0.0f;
    float  _waveInterval = 10.0f;
    float  _scoreRate = 100.0f;
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
};

// Draws the player HUD (score / kills / level / XP) each tick while active.
class HUDComponent : public Component {
    GameDirectorComponent* _director;
    PlayerComponent*       _player = nullptr;
    FontHandle                 _fontID;
public:
    HUDComponent(GameObject* go, GameDirectorComponent* director, FontHandle fontID)
        : _director(director), _fontID(fontID) { gameObject = go; }

    std::string GetName() const override { return "HUDComponent"; }

    void SetPlayer(PlayerComponent* p) { _player = p; }

    void OnTick(float /*dt*/) override {
        if (!_player || !_director) return;
        auto* gs = gameObject->GetApp()->GetGraphicsSubsystem();
        std::ostringstream oss;
        oss << "Score: " << std::setw(8) << std::setfill('0') << (long long)_director->Score()
            << "  Kills: " << std::setw(4) << std::setfill('0') << _director->Kills()
            << "  Lv." << _player->Level()
            << " (" << std::fixed << std::setprecision(1)
            << (_player->XP() / _player->NextXP() * 100.0f) << "%)";
        gs->DrawText(_fontID, oss.str(), 10.0f, 10.0f, 0.8f, glm::vec4(1,1,0,1));
    }
};

// Tracks spawned bullets and enemies; resolves AABB collisions and flushes
// inactive objects each tick. Active only during GameState::Playing.
class CollisionSystemComponent : public Component {
    GameDirectorComponent*   _director;
    PlayerComponent*         _player = nullptr;
    std::function<void()>    _onExplosion;
    std::vector<GameObject*> _bullets;
    std::vector<GameObject*> _enemies;
public:
    CollisionSystemComponent(GameObject* go, GameDirectorComponent* director,
                             std::function<void()> onExplosion)
        : _director(director), _onExplosion(std::move(onExplosion)) { gameObject = go; }

    std::string GetName() const override { return "CollisionSystemComponent"; }

    void SetPlayer(PlayerComponent* p) { _player = p; }

    void AddBullet(GameObject* b) { _bullets.push_back(b); }
    void AddEnemy (GameObject* e) { _enemies.push_back(e); }

    void ClearAll() {
        for (auto* b : _bullets) b->SetActive(false);
        for (auto* e : _enemies) e->SetActive(false);
        _bullets.clear();
        _enemies.clear();
    }

    void OnTick(float /*dt*/) override {
        checkCollisions();
        flushDead();
    }

    void DrawImGui() override {
        ImGui::Text("Bullets tracked: %d", static_cast<int>(_bullets.size()));
        ImGui::Text("Enemies tracked: %d", static_cast<int>(_enemies.size()));
    }

private:
    void checkCollisions() {
        for (auto* b : _bullets) {
            if (!b->isActive) continue;
            auto* bm = b->GetComponent<BulletMovementComponent>();
            if (!bm || bm->GetType() < 0) continue;
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
    }

    void killEnemy(GameObject* enemy) {
        enemy->SetActive(false);
        auto* em = enemy->GetComponent<EnemyMovementComponent>();
        int level = em ? em->GetLevel() : 1;
        _director->AddScore(100.0);
        _director->AddKill();
        if (_player) _player->AddXP(level);
        if (_onExplosion) _onExplosion();
    }

    void flushDead() {
        auto inactive = [](GameObject* g) { return !g->isActive; };
        _bullets.erase(std::remove_if(_bullets.begin(), _bullets.end(), inactive), _bullets.end());
        _enemies.erase(std::remove_if(_enemies.begin(), _enemies.end(), inactive), _enemies.end());
    }

    static bool aabb(float ax, float ay, float aw, float ah,
                     float bx, float by, float bw, float bh) {
        return std::abs(ax - bx) < (aw + bw) * 0.5f &&
               std::abs(ay - by) < (ah + bh) * 0.5f;
    }
};

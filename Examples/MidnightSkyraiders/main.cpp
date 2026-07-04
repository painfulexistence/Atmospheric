#include "Atmospheric.hpp"
#include "components.hpp"

#include <cmath>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// Wave data: 24 waves × 5 rows × 8 cols  (0=empty, 1-3=enemy type)
// ─────────────────────────────────────────────────────────────────────────────
static const int waves[24][5][8] = {
    { { 0, 1, 1, 0, 0, 0, 0, 0 },
      { 0, 0, 0, 0, 0, 0, 0, 0 },
      { 0, 0, 0, 0, 0, 0, 0, 0 },
      { 0, 0, 0, 0, 0, 0, 0, 0 },
      { 0, 1, 1, 0, 0, 0, 0, 0 } },
    { { 0, 0, 1, 0, 0, 0, 0, 0 },
      { 0, 1, 0, 0, 0, 0, 0, 0 },
      { 0, 0, 0, 0, 0, 0, 0, 0 },
      { 0, 1, 0, 0, 0, 0, 0, 0 },
      { 0, 0, 1, 0, 0, 0, 0, 0 } },
    { { 0, 1, 0, 1, 0, 0, 0, 0 },
      { 0, 0, 0, 0, 0, 0, 0, 0 },
      { 0, 0, 0, 0, 0, 0, 0, 0 },
      { 0, 0, 0, 0, 0, 0, 0, 0 },
      { 0, 1, 0, 1, 0, 0, 0, 0 } },
    { { 0, 0, 1, 0, 0, 0, 0, 0 },
      { 0, 1, 0, 0, 0, 0, 0, 0 },
      { 1, 0, 0, 0, 0, 0, 0, 0 },
      { 0, 1, 0, 0, 0, 0, 0, 0 },
      { 0, 0, 1, 0, 0, 0, 0, 0 } },
    { { 0, 0, 1, 0, 0, 0, 0, 0 },
      { 0, 0, 1, 0, 0, 0, 0, 0 },
      { 0, 0, 0, 0, 0, 0, 0, 0 },
      { 0, 0, 1, 0, 0, 0, 0, 0 },
      { 0, 0, 1, 0, 0, 0, 0, 0 } },
    { { 1, 0, 0, 0, 0, 0, 0, 0 },
      { 0, 1, 0, 0, 0, 0, 0, 0 },
      { 0, 0, 0, 0, 0, 0, 0, 0 },
      { 0, 1, 0, 0, 0, 0, 0, 0 },
      { 0, 0, 1, 0, 0, 0, 0, 0 } },
    { { 0, 0, 1, 0, 0, 0, 0, 0 },
      { 0, 1, 0, 0, 0, 0, 0, 0 },
      { 0, 0, 0, 0, 0, 0, 0, 0 },
      { 0, 1, 0, 0, 0, 0, 0, 0 },
      { 1, 0, 0, 0, 0, 0, 0, 0 } },
    { { 0, 0, 0, 0, 0, 0, 0, 0 },
      { 1, 0, 1, 0, 0, 0, 0, 0 },
      { 0, 0, 0, 0, 0, 0, 0, 0 },
      { 1, 0, 1, 0, 0, 0, 0, 0 },
      { 0, 0, 0, 0, 0, 0, 0, 0 } },
    { { 0, 0, 1, 0, 1, 0, 0, 0 },
      { 0, 1, 0, 1, 0, 0, 0, 0 },
      { 1, 0, 1, 0, 0, 0, 0, 0 },
      { 0, 1, 0, 1, 0, 0, 0, 0 },
      { 0, 0, 1, 0, 1, 0, 0, 0 } },
    { { 0, 0, 1, 0, 2, 0, 0, 0 },
      { 0, 1, 0, 1, 0, 0, 0, 0 },
      { 1, 0, 1, 0, 0, 0, 0, 0 },
      { 0, 1, 0, 1, 0, 0, 0, 0 },
      { 0, 0, 1, 0, 2, 0, 0, 0 } },
    { { 0, 0, 0, 0, 0, 0, 0, 0 },
      { 0, 1, 0, 0, 1, 0, 0, 0 },
      { 1, 1, 1, 1, 1, 1, 0, 0 },
      { 0, 1, 0, 0, 1, 0, 0, 0 },
      { 0, 0, 0, 0, 0, 0, 0, 0 } },
    { { 0, 0, 0, 0, 0, 0, 0, 0 },
      { 0, 1, 0, 0, 1, 0, 0, 0 },
      { 1, 1, 1, 1, 3, 1, 0, 0 },
      { 0, 1, 0, 0, 1, 0, 0, 0 },
      { 0, 0, 0, 0, 0, 0, 0, 0 } },
    { { 0, 1, 1, 1, 1, 0, 0, 0 },
      { 0, 0, 0, 0, 0, 0, 0, 0 },
      { 0, 0, 0, 0, 0, 0, 0, 0 },
      { 0, 0, 0, 0, 0, 0, 0, 0 },
      { 0, 1, 1, 1, 1, 0, 0, 0 } },
    { { 0, 1, 2, 1, 2, 0, 0, 0 },
      { 0, 0, 0, 0, 0, 0, 0, 0 },
      { 0, 0, 0, 0, 0, 0, 0, 0 },
      { 0, 0, 0, 0, 0, 0, 0, 0 },
      { 0, 2, 1, 2, 1, 0, 0, 0 } },
    { { 0, 2, 1, 2, 1, 0, 0, 0 },
      { 0, 0, 0, 0, 0, 0, 0, 0 },
      { 0, 0, 0, 0, 0, 0, 0, 0 },
      { 0, 0, 0, 0, 0, 0, 0, 0 },
      { 0, 1, 2, 1, 2, 0, 0, 0 } },
    { { 1, 0, 0, 0, 1, 0, 0, 0 },
      { 0, 1, 0, 1, 0, 0, 0, 0 },
      { 0, 0, 1, 0, 0, 0, 0, 0 },
      { 0, 1, 0, 1, 0, 0, 0, 0 },
      { 1, 0, 0, 0, 1, 0, 0, 0 } },
    { { 1, 0, 0, 0, 1, 0, 0, 0 },
      { 0, 2, 0, 2, 0, 0, 0, 0 },
      { 0, 0, 3, 0, 0, 0, 0, 0 },
      { 0, 2, 0, 2, 0, 0, 0, 0 },
      { 1, 0, 0, 0, 1, 0, 0, 0 } },
    { { 0, 0, 0, 0, 0, 0, 0, 0 },
      { 0, 1, 1, 1, 0, 0, 0, 0 },
      { 0, 1, 0, 1, 0, 0, 0, 0 },
      { 0, 1, 1, 1, 0, 0, 0, 0 },
      { 0, 0, 0, 0, 0, 0, 0, 0 } },
    { { 0, 0, 0, 0, 0, 0, 0, 0 },
      { 0, 1, 1, 1, 0, 0, 0, 0 },
      { 0, 1, 3, 1, 0, 0, 0, 0 },
      { 0, 1, 1, 1, 0, 0, 0, 0 },
      { 0, 0, 0, 0, 0, 0, 0, 0 } },
    { { 0, 0, 0, 0, 0, 0, 0, 0 },
      { 0, 2, 0, 1, 0, 2, 0, 1 },
      { 0, 1, 0, 1, 0, 1, 0, 1 },
      { 0, 1, 0, 2, 0, 1, 0, 2 },
      { 0, 0, 0, 0, 0, 0, 0, 0 } },
    { { 1, 0, 0, 0, 0, 0, 1, 0 },
      { 0, 1, 0, 1, 0, 1, 0, 0 },
      { 0, 0, 1, 0, 1, 0, 0, 0 },
      { 0, 1, 0, 1, 0, 1, 0, 0 },
      { 1, 0, 0, 0, 0, 0, 1, 0 } },
    { { 1, 0, 0, 0, 0, 0, 1, 0 },
      { 0, 1, 0, 1, 0, 1, 0, 0 },
      { 0, 0, 3, 0, 3, 0, 0, 0 },
      { 0, 1, 0, 1, 0, 1, 0, 0 },
      { 1, 0, 0, 0, 0, 0, 1, 0 } },
    { { 0, 0, 1, 0, 1, 0, 1, 0 },
      { 0, 1, 0, 1, 0, 1, 0, 0 },
      { 0, 0, 0, 0, 0, 0, 0, 0 },
      { 0, 1, 0, 1, 0, 1, 0, 0 },
      { 0, 0, 1, 0, 1, 0, 1, 0 } },
    { { 0, 0, 2, 0, 2, 0, 1, 0 },
      { 0, 3, 0, 1, 0, 1, 0, 0 },
      { 0, 0, 0, 0, 0, 0, 0, 0 },
      { 0, 3, 0, 1, 0, 1, 0, 0 },
      { 0, 0, 2, 0, 2, 0, 1, 0 } },
};

enum class GameState { Title, Playing, GameOver };

// ─────────────────────────────────────────────────────────────────────────────
// Application
// ─────────────────────────────────────────────────────────────────────────────

class MidnightSkyraiders : public Application {
    using Application::Application;

    GameState _state = GameState::Title;

    // Textures
    TextureHandle _texPlayer, _texEnemy1, _texEnemy2, _texEnemy3;
    TextureHandle _texBullet, _texCircle, _texOrbit;
    TextureHandle _texEBullet, _texECircle;
    TextureHandle _texBg[3] = {};
    FontHandle _fontID = 0;

    MusicID _bgm = 0;
    SoundID _sfxExp = 0;
    SoundID _sfxOver = 0;

    static constexpr float bgSpeed[3] = { 5.0f, 40.0f, 120.0f };

    // Title
    GameObject* _titleObj = nullptr;

    // Player — progression state lives in PlayerComponent (editor-inspectable)
    GameObject* _playerObj = nullptr;
    PlayerComponent* _player = nullptr;
    static constexpr float plW = 32.0f, plH = 32.0f;

    // World director — score / waves / system-fire throttle (editor-inspectable)
    GameObject* _worldObj = nullptr;
    GameDirectorComponent* _director = nullptr;

    CollisionSystemComponent* _collision = nullptr;
    HUDComponent* _hudComp = nullptr;

    long long _hiScore = 0;

    // ── lifecycle ──────────────────────────────────────────────────────────────

    void OnInit() override {
        ComponentFactory::Register("ParallaxLayerComponent", [](GameObject* o, Deserializer& d) -> Component* {
            int textureID = 0, zOrder = 0;
            float worldSize = 0.0f, speed = 0.0f;
            d.Read("textureID", textureID);
            d.Read("worldSize", worldSize);
            d.Read("speed", speed);
            d.Read("zOrder", zOrder);
            return new ParallaxLayerComponent(o, textureID, worldSize, speed, zOrder);
        });
        ComponentFactory::Register("BulletMovementComponent", [](GameObject* o, Deserializer& d) -> Component* {
            int type = 0;
            float x = 0.0f, y = 0.0f, maxLife = 0.0f;
            d.Read("type", type);
            d.Read("x", x);
            d.Read("y", y);
            d.Read("maxLife", maxLife);
            return new BulletMovementComponent(o, type, x, y, maxLife);
        });
        ComponentFactory::Register("EnemyMovementComponent", [](GameObject* o, Deserializer& d) -> Component* {
            int type = 0, level = 0;
            float speed = 0.0f, raidDist = 0.0f, raidStart = 0.0f, raidDur = 0.0f;
            d.Read("type", type);
            d.Read("level", level);
            d.Read("speed", speed);
            d.Read("raidDist", raidDist);
            d.Read("raidStart", raidStart);
            d.Read("raidDur", raidDur);
            return new EnemyMovementComponent(o, type, level, speed, raidDist, raidStart, raidDur);
        });
        ComponentFactory::Register("PlayerComponent", [](GameObject* o, Deserializer& d) -> Component* {
            return new PlayerComponent(o);
        });
        GoScene("main", [this] { OnLoad(); });
    }

    void OnLoad() override {

        _texPlayer = "assets/images/player.png";
        _texEnemy1 = "assets/images/enemy1.png";
        _texEnemy2 = "assets/images/enemy2.png";
        _texEnemy3 = "assets/images/enemy3.png";
        _texBullet = "assets/images/bullet.png";
        _texCircle = "assets/images/circle-bullet.png";
        _texOrbit = "assets/images/orbit-bullet.png";
        _texEBullet = "assets/images/enemy-bullet.png";
        _texECircle = "assets/images/enemy-circle-bullet.png";
        _texBg[0] = "assets/images/nightsky-bg.png";
        _texBg[1] = "assets/images/nightsky-mountains.png";
        _texBg[2] = "assets/images/nightsky-fg.png";
        // The title sprite (and its texture) live in assets/scenes/main.json.

        _fontID = GraphicsSubsystem::Get()->LoadFont("assets/fonts/NotoSans-SemiBold.ttf", 22.0f);

        _bgm = AudioSubsystem::Get()->LoadMusic("assets/sounds/sky-lines.ogg");
        _sfxExp = AudioSubsystem::Get()->LoadSound("assets/sounds/explosion.wav");
        _sfxOver = AudioSubsystem::Get()->LoadSound("assets/sounds/game-over.wav");

        auto* background = CreateGameObject();
        background->SetName("Background");
        for (int i = 0; i < 3; i++) {
            background->AddComponent<ParallaxLayerComponent>(_texBg[i], WORLD, bgSpeed[i], i);
        }

        // World director: a single "GameWorld" entity owns match-wide state.
        // Wave spawns are deferred because the director ticks inside the loop.
        _worldObj = CreateGameObject();
        _worldObj->SetName("GameWorld");
        _director = static_cast<GameDirectorComponent*>(_worldObj->AddComponent<GameDirectorComponent>([this] {
            DeferSpawn([this] { SpawnWave(); });
        }));

        auto* sysObj = CreateGameObject();
        sysObj->SetName("CollisionSystem");
        sysObj->SetActive(false);
        sysObj->AddComponent<CollisionSystemComponent>(_director, [this] {
            AudioSubsystem::Get()->PlaySoundVariation(_sfxExp, 0.1f, 0.05f);
        });
        _collision = sysObj->GetComponent<CollisionSystemComponent>();

        auto* hudObj = CreateGameObject();
        hudObj->SetName("HUD");
        hudObj->SetActive(false);
        hudObj->AddComponent<HUDComponent>(_director, _fontID);
        _hudComp = hudObj->GetComponent<HUDComponent>();

        EnterTitle();
    }

    void OnUpdate(float dt, float /*time*/) override {
        switch (_state) {
        case GameState::Title:
            UpdateTitle(dt);
            break;
        case GameState::Playing:
            break;// CollisionSystemComponent + HUDComponent drive per-frame logic
        case GameState::GameOver:
            UpdateGameOver(dt);
            break;
        }
        if (InputSubsystem::Get()->IsKeyDown(Key::ESCAPE)) Quit();
    }

    // ── title ─────────────────────────────────────────────────────────────────

    void EnterTitle() {
        _state = GameState::Title;
        // The title sprite is instantiated from assets/scenes/main.json. Grab a
        // reference to it so updateTitle can hide it once the player starts.
        _titleObj = nullptr;
        for (const auto& e : GetEntities()) {
            if (e->GetName() == "Title") {
                _titleObj = e.get();
                break;
            }
        }
    }

    void UpdateTitle(float /*dt*/) {
        auto* gs = GraphicsSubsystem::Get();
        gs->DrawText(
            _fontID, "Press SPACE or ENTER to start", HALF - 140.0f, WORLD - 96.0f, 0.8f, glm::vec4(1, 1, 0, 1)
        );

        // Credits, mirrored from the original game's index.html.
        gs->DrawText(
            _fontID, "Arts & Programming: DevLucidum", HALF - 100.0f, WORLD - 52.0f, 0.55f, glm::vec4(1, 1, 1, 1)
        );
        gs->DrawText(
            _fontID, "Music: \"Sky - Lines\" by Ansimuz", HALF - 100.0f, WORLD - 30.0f, 0.55f, glm::vec4(1, 1, 1, 1)
        );

        if (InputSubsystem::Get()->IsKeyPressed(Key::SPACE) || InputSubsystem::Get()->IsKeyPressed(Key::ENTER)) {
            if (_titleObj) {
                _titleObj->SetActive(false);
                _titleObj = nullptr;
            }
            StartGame();
        }
    }

    // ── game ──────────────────────────────────────────────────────────────────

    void StartGame() {
        if (_collision) _collision->ClearAll();
        if (_playerObj) {
            _playerObj->SetActive(false);
            _playerObj = nullptr;
        }

        _director->Reset();

        _playerObj = CreateGameObject(glm::vec2(toEngX(-264.0f), toEngY(-16.0f)));
        _playerObj->SetName("Player");
        _playerObj->AddComponent<SpriteComponent>(SpriteProps{
            .size = glm::vec2(plW, plH),
            .pivot = glm::vec2(0.5f, 0.5f),
            .color = glm::vec4(1, 1, 1, 1),
            .texture = _texPlayer,
            .layer = CanvasLayer::LAYER_WORLD_2D,
            .flipY = true,
            .zOrder = 5,
        });
        _player = static_cast<PlayerComponent*>(_playerObj->AddComponent<PlayerComponent>());
        _playerObj->AddComponent<PlayerInputComponent>(_player, plW, plH, [this](float px, float py) {
            SpawnPlayerBulletsDeferred(px, py);
        });

        if (_collision) {
            _collision->SetPlayer(_player);
            _collision->gameObject->SetActive(true);
        }
        if (_hudComp) {
            _hudComp->SetPlayer(_player);
            _hudComp->gameObject->SetActive(true);
        }

        SpawnWave();
        AudioSubsystem::Get()->PlayMusic(_bgm);
        _director->SetRunning(true);
        _state = GameState::Playing;
    }


    // ── spawn ─────────────────────────────────────────────────────────────────

    // Called from PlayerInputComponent::OnTick — must defer because we are
    // inside the entity tick loop and CreateGameObject would invalidate it.
    void SpawnPlayerBulletsDeferred(float px, float py) {
        float bx = px + plW * 0.5f;
        DeferSpawn([this, bx, py] { SpawnBullet(0, bx, py); });
        DeferSpawn([this, bx, py] { SpawnBullet(1, bx, py); });
        DeferSpawn([this, bx, py] { SpawnBullet(2, bx, py); });
    }

    void SpawnBullet(int type, float x, float y) {
        float maxLife = 99999.0f;
        if (type == 2) maxLife = 2.0f;
        if (type == -3) maxLife = 2.5f;

        // Render each bullet at its texture's native size. bullet.png and
        // enemy-bullet.png are 32x4 laser streaks; forcing everything to 8x8
        // collapsed them into tiny squares, which is why the normal player
        // shot and the enemy laser looked missing. Sizes mirror the source
        // game's per-type sprites (32x4 lasers, 16x16 orbs, 8x8 orbit).
        TextureHandle tex = _texBullet;
        glm::vec2 sz(32.0f, 4.0f);
        switch (type) {
        case 0:
            tex = _texBullet;
            sz = glm::vec2(32.0f, 4.0f);
            break;
        case 1:
            tex = _texCircle;
            sz = glm::vec2(16.0f, 16.0f);
            break;
        case 2:
            tex = _texOrbit;
            sz = glm::vec2(8.0f, 8.0f);
            break;
        case -1:
            tex = _texECircle;
            sz = glm::vec2(16.0f, 16.0f);
            break;
        case -2:
            tex = _texEBullet;
            sz = glm::vec2(32.0f, 4.0f);
            break;
        case -3:
            tex = _texECircle;
            sz = glm::vec2(16.0f, 16.0f);
            break;
        }

        auto* obj = CreateGameObject(glm::vec2(x, y));
        obj->AddComponent<SpriteComponent>(SpriteProps{
            .size = sz,
            .pivot = glm::vec2(0.5f, 0.5f),
            .color = glm::vec4(1, 1, 1, 1),
            .texture = tex,
            .layer = CanvasLayer::LAYER_WORLD_2D,
            .flipY = true,
            .zOrder = 3,
        });
        obj->AddComponent<BulletMovementComponent>(type, x, y, maxLife);
        if (_collision) _collision->AddBullet(obj);
    }

    void SpawnWave() {
        _director->OnWaveSpawned();
        int wave = _director->WaveCount();
        int idx = static_cast<int>(rnd() * 23);
        float ox = (rnd() - 0.5f) * 30.0f;
        float oy = (rnd() - 0.5f) * 30.0f;

        for (int row = 0; row < 5; row++) {
            for (int col = 0; col < 8; col++) {
                int cell = waves[idx][row][col];
                if (cell == 0) continue;
                float ex = toEngX(col * 90.0f - 300.0f + ox) + WORLD;
                float ey = toEngY(row * 90.0f - 300.0f + 100.0f + oy);
                SpawnEnemy(cell, ex, ey, wave);
            }
        }
    }

    void SpawnEnemy(int type, float x, float y, int lvl) {
        float spd = std::min(std::floor(lvl / 5.0f + 1.0f) * 50.0f, 500.0f);
        float fireCD = std::max(2.0f - 0.1f * lvl, 0.5f);
        float raidDur = (type == 1) ? 0.0f : (type == 2 ? 1.0f : 2.0f);
        float raidDist = (y < HALF ? +1.0f : -1.0f) * 600.0f * rnd();
        float raidStart = (raidDur > 0.0f) ? (1000.0f / spd) * rnd() : 0.0f;

        TextureHandle tex = (type == 1) ? _texEnemy1 : (type == 2 ? _texEnemy2 : _texEnemy3);
        auto* obj = CreateGameObject(glm::vec2(x, y));
        obj->AddComponent<SpriteComponent>(SpriteProps{
            .size = glm::vec2(32.0f, 32.0f),
            .pivot = glm::vec2(0.5f, 0.5f),
            .color = glm::vec4(1, 1, 1, 1),
            .texture = tex,
            .layer = CanvasLayer::LAYER_WORLD_2D,
            .flipY = true,
            .zOrder = 4,
        });
        obj->AddComponent<EnemyMovementComponent>(type, lvl, spd, raidDist, raidStart, raidDur);
        // Enemy bullets are deferred: EnemyFireComponent is inside the tick loop.
        obj->AddComponent<EnemyFireComponent>(
            type, fireCD, fireCD * rnd(), _director->SysFireTimer(), [this](int bt, float fx, float fy) {
                DeferSpawn([this, bt, fx, fy] { SpawnBullet(bt, fx, fy); });
            }
        );
        if (_collision) _collision->AddEnemy(obj);
    }

    // ── game over ─────────────────────────────────────────────────────────────

    void TriggerGameOver() {
        _director->SetRunning(false);
        if (_collision) _collision->gameObject->SetActive(false);
        if (_hudComp) _hudComp->gameObject->SetActive(false);
        if (static_cast<long long>(_director->Score()) > _hiScore)
            _hiScore = static_cast<long long>(_director->Score());
        AudioSubsystem::Get()->StopMusic(_bgm);
        AudioSubsystem::Get()->PlaySound(_sfxOver);
        _state = GameState::GameOver;
    }

    void UpdateGameOver(float /*dt*/) {
        auto* gs = GraphicsSubsystem::Get();
        gs->DrawText(_fontID, "GAME  OVER", HALF - 80.0f, WORLD * 0.30f, 1.5f, glm::vec4(1, 0.2f, 0.2f, 1));
        gs->DrawText(
            _fontID,
            "Score: " + std::to_string(static_cast<long long>(_director->Score())),
            HALF - 80.0f,
            WORLD * 0.45f,
            1.0f,
            glm::vec4(1, 1, 1, 1)
        );
        gs->DrawText(
            _fontID, "Best:  " + std::to_string(_hiScore), HALF - 80.0f, WORLD * 0.52f, 1.0f, glm::vec4(1, 1, 0, 1)
        );
        gs->DrawText(
            _fontID,
            "Press SPACE or ENTER to play again",
            HALF - 150.0f,
            WORLD * 0.65f,
            0.8f,
            glm::vec4(0.8f, 0.8f, 0.8f, 1)
        );
        if (InputSubsystem::Get()->IsKeyPressed(Key::SPACE) || InputSubsystem::Get()->IsKeyPressed(Key::ENTER)) {
            StartGame();
        }
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
    // title.png is declared in assets/scenes/main.json and prefetched/loaded by
    // the scene system, so it is intentionally not listed here.
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
    static MidnightSkyraiders game(
        {
            .windowTitle = "Midnight Skyraiders",
            .windowWidth = 600,
            .windowHeight = 600,
            .enablePhysics3D = false,
            .useDefaultTextures = true,
            .useDefaultShaders = true,
        }
    );
    game.Run();
}
#else
int main(int argc, char* argv[]) {
    MidnightSkyraiders game(
        {
            .windowTitle = "Midnight Skyraiders",
            .windowWidth = 600,
            .windowHeight = 600,
            .enablePhysics3D = false,
            .useDefaultTextures = true,
            .useDefaultShaders = true,
        }
    );
    game.Run();
    return 0;
}
#endif

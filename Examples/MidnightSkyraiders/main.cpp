#include "Atmospheric.hpp"
#include "components.hpp"

#include <vector>
#include <cmath>
#include <string>

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

enum class GameState { Title, Playing, GameOver };

// ─────────────────────────────────────────────────────────────────────────────
// Application
// ─────────────────────────────────────────────────────────────────────────────

class MidnightSkyraiders : public Application {
    using Application::Application;

    GameState state = GameState::Title;

    // Textures
    TextureHandle texPlayer, texEnemy1, texEnemy2, texEnemy3;
    TextureHandle texBullet, texCircle, texOrbit;
    TextureHandle texEBullet, texECircle;
    TextureHandle texBg[3]   = {};
    TextureHandle texTitle;
    FontHandle fontID     = 0;

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

    CollisionSystemComponent* _collision = nullptr;
    HUDComponent*             _hudComp   = nullptr;

    long long hiScore = 0;

    // ── lifecycle ──────────────────────────────────────────────────────────────

    void OnInit() override {
        ComponentFactory::Register("ParallaxLayerComponent",
          [](GameObject* o, Deserializer& d) -> Component* {
              int textureID = 0, zOrder = 0;
              float worldSize = 0.0f, speed = 0.0f;
              d.Read("textureID", textureID);
              d.Read("worldSize", worldSize);
              d.Read("speed", speed);
              d.Read("zOrder", zOrder);
              return new ParallaxLayerComponent(o, textureID, worldSize, speed, zOrder);
          });
        ComponentFactory::Register("BulletMovementComponent",
          [](GameObject* o, Deserializer& d) -> Component* {
              int type = 0;
              float x = 0.0f, y = 0.0f, maxLife = 0.0f;
              d.Read("type", type);
              d.Read("x", x);
              d.Read("y", y);
              d.Read("maxLife", maxLife);
              return new BulletMovementComponent(o, type, x, y, maxLife);
          });
        ComponentFactory::Register("EnemyMovementComponent",
          [](GameObject* o, Deserializer& d) -> Component* {
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
        ComponentFactory::Register("PlayerComponent",
          [](GameObject* o, Deserializer& d) -> Component* {
              return new PlayerComponent(o);
          });
        GoScene("main", [this]{ OnLoad(); });
    }

    void OnLoad() override {

        texPlayer  = "assets/images/player.png";
        texEnemy1  = "assets/images/enemy1.png";
        texEnemy2  = "assets/images/enemy2.png";
        texEnemy3  = "assets/images/enemy3.png";
        texBullet  = "assets/images/bullet.png";
        texCircle  = "assets/images/circle-bullet.png";
        texOrbit   = "assets/images/orbit-bullet.png";
        texEBullet = "assets/images/enemy-bullet.png";
        texECircle = "assets/images/enemy-circle-bullet.png";
        texBg[0]   = "assets/images/nightsky-bg.png";
        texBg[1]   = "assets/images/nightsky-mountains.png";
        texBg[2]   = "assets/images/nightsky-fg.png";
        texTitle   = "assets/images/title.png";

        fontID = GraphicsServer::Get()->LoadFont("assets/fonts/NotoSans-SemiBold.ttf", 22.0f);

        bgm     = audio.LoadMusic("assets/sounds/sky-lines.ogg");
        sfxExp  = audio.LoadSound("assets/sounds/explosion.wav");
        sfxOver = audio.LoadSound("assets/sounds/game-over.wav");

        auto* background = CreateGameObject();
        background->SetName("Background");
        for (int i = 0; i < 3; i++) {
            background->AddComponent<ParallaxLayerComponent>(texBg[i], WORLD, BG_SPEED[i], i);
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

        auto* sysObj = CreateGameObject();
        sysObj->SetName("CollisionSystem");
        sysObj->SetActive(false);
        sysObj->AddComponent<CollisionSystemComponent>(director,
            [this]{ audio.PlaySoundVariation(sfxExp, 0.1f, 0.05f); });
        _collision = sysObj->GetComponent<CollisionSystemComponent>();

        auto* hudObj = CreateGameObject();
        hudObj->SetName("HUD");
        hudObj->SetActive(false);
        hudObj->AddComponent<HUDComponent>(director, fontID);
        _hudComp = hudObj->GetComponent<HUDComponent>();

        enterTitle();
    }

    void OnUpdate(float dt, float /*time*/) override {
        switch (state) {
        case GameState::Title:    updateTitle(dt);    break;
        case GameState::Playing:  break; // CollisionSystemComponent + HUDComponent drive per-frame logic
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
            .texture   = texTitle,
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
        if (_collision) _collision->ClearAll();
        if (playerObj) { playerObj->SetActive(false); playerObj = nullptr; }

        director->Reset();

        playerObj = CreateGameObject(glm::vec2(toEngX(-264.0f), toEngY(-16.0f)));
        playerObj->SetName("Player");
        playerObj->AddComponent<SpriteComponent>(SpriteProps{
            .size      = glm::vec2(PL_W, PL_H),
            .pivot     = glm::vec2(0.5f, 0.5f),
            .color     = glm::vec4(1,1,1,1),
            .texture   = texPlayer,
            .layer     = CanvasLayer::LAYER_WORLD_2D,
            .flipY     = true,
            .zOrder    = 5,
        });
        player = static_cast<PlayerComponent*>(playerObj->AddComponent<PlayerComponent>());
        playerObj->AddComponent<PlayerInputComponent>(
            player, PL_W, PL_H,
            [this](float px, float py) { spawnPlayerBulletsDeferred(px, py); }
        );

        if (_collision) {
            _collision->SetPlayer(player);
            _collision->gameObject->SetActive(true);
        }
        if (_hudComp) {
            _hudComp->SetPlayer(player);
            _hudComp->gameObject->SetActive(true);
        }

        spawnWave();
        audio.PlayMusic(bgm);
        director->SetRunning(true);
        state = GameState::Playing;
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

        TextureHandle tex = texBullet;
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
            .texture   = tex,
            .layer     = CanvasLayer::LAYER_WORLD_2D,
            .flipY     = true,
            .zOrder    = 3,
        });
        obj->AddComponent<BulletMovementComponent>(type, x, y, maxLife);
        if (_collision) _collision->AddBullet(obj);
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

        TextureHandle tex = (type == 1) ? texEnemy1 : (type == 2 ? texEnemy2 : texEnemy3);
        auto* obj = CreateGameObject(glm::vec2(x, y));
        obj->AddComponent<SpriteComponent>(SpriteProps{
            .size      = glm::vec2(32.0f, 32.0f),
            .pivot     = glm::vec2(0.5f, 0.5f),
            .color     = glm::vec4(1,1,1,1),
            .texture   = tex,
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
        if (_collision) _collision->AddEnemy(obj);
    }

    // ── game over ─────────────────────────────────────────────────────────────

    void triggerGameOver() {
        director->SetRunning(false);
        if (_collision) _collision->gameObject->SetActive(false);
        if (_hudComp)   _hudComp->gameObject->SetActive(false);
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

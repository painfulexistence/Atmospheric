#include "Atmospheric.hpp"
#include "components.hpp"
#include <Atmospheric/gfx_factory.hpp>
#include <Atmospheric/lighting_2d.hpp>
#include <Atmospheric/tilemap_2d.hpp>
#include <algorithm>
#include <cmath>
#include <fmt/format.h>

using glm::vec2;
using glm::vec4;

class RPGGame : public Application {
    // ── resources ──────────────────────────────────────────────────────
    uint32_t _tilesetTex = 0, _playerTex = 0, _enemyTex = 0, _npcTex = 0;
    FontHandle _fontID = 0;

    std::unique_ptr<Tilemap2D> _tilemap;
    LightingSystem2D _lighting;

    // ── world entities ─────────────────────────────────────────────────
    Player _player;
    GameObject* _playerAnimGO = nullptr;// host for the player's FlipbookComponent
    FlipbookComponent* _playerFlip = nullptr;
    std::vector<Enemy> _enemies;
    std::vector<FlipbookComponent*> _enemyFlips;// one per enemy, hosted on its AI GameObject
    std::vector<NPC> _npcs;

    // ── camera ─────────────────────────────────────────────────────────
    float _camX = 0, _camY = 0;
    int _screenW = 800, _screenH = 600;

    // ── game state ─────────────────────────────────────────────────────
    GameMode _mode = GameMode::Explore;
    float _fadeIn = 1.0f;
    float _transition = 0.0f;
    std::string _dialogText;
    float _dialogTimer = 0.0f;
    float _time = 0.0f;

    // ── battle (owned by BattleSystemComponent) ────────────────────────
    BattleSystemComponent* _battleComp = nullptr;

public:
    RPGGame()
      : Application(
            AppConfig{
                .windowTitle = "RPG Demo",
                .windowWidth = 800,
                .windowHeight = 600,
                .enableGraphics3D = false,
                .enablePhysics3D = false,
            }
        ) {
    }

    void OnInit() override {
        _screenW = 800;
        _screenH = 600;
        GoScene("main", [this] { OnLoad(); });
    }

    void OnLoad() override {
        auto* gfx = GraphicsSubsystem::Get();
        _fontID = gfx->LoadFont("assets/fonts/NotoSans-SemiBold.ttf", 20.0f);

        // Tileset (4×2, 32px)
        constexpr int ts = 32, tc = 4, tr = 2;
        _tilesetTex = GfxFactory::UploadTexture2D(
            MakeColorSheetPixels(
                ts,
                tc,
                tr,
                {
                    { 80, 120, 60 },
                    { 60, 50, 40 },
                    { 100, 100, 110 },
                    { 40, 80, 130 },
                    { 120, 90, 50 },
                    { 80, 80, 80 },
                    { 60, 100, 50 },
                    { 180, 160, 120 },
                }
            )
                .data(),
            tc * ts,
            tr * ts,
            TextureFilter::Nearest
        );// pixel-art tiles

        // Tilemap
        const std::vector<std::string> map = {
            "3333333333333333333333333", "3111111111111111111111113", "3111111111222111111111113",
            "3111221111222111113311113", "3111221111111111113311113", "3111111111111111111111113",
            "3111111111111111111111113", "3111111111111111111111113", "3133111111111111111331113",
            "3133111111111111111331113", "3111111111111111111111113", "3111111222211111122111113",
            "3111111222211111122111113", "3111111111111111111111113", "3111111111111111111111113",
            "3111441111111111114411113", "3111441111111111114411113", "3111111111111111111111113",
            "3111111111111111111111113", "3333333333333333333333333",
        };
        Tilemap2DData md;
        md.width = static_cast<int>(map[0].size());
        md.height = static_cast<int>(map.size());
        md.tileSize = ts;
        md.tilesetCols = tc;
        md.tilesetRows = tr;
        md.solid = { 3, 4 };
        for (const auto& row : map)
            for (char c : row)
                md.tiles.push_back(c - '0');
        _tilemap = std::make_unique<Tilemap2D>(md, _tilesetTex);

        // Character sheets (4×2 of 24px)
        constexpr int cs = 24, cc = 4, cr = 2;
        _playerTex = GfxFactory::UploadTexture2D(
            MakeColorSheetPixels(
                cs,
                cc,
                cr,
                {
                    { 70, 120, 200 },
                    { 60, 110, 190 },
                    { 80, 130, 210 },
                    { 65, 115, 195 },
                    { 50, 100, 180 },
                    { 75, 125, 205 },
                    { 85, 140, 215 },
                    { 55, 105, 185 },
                }
            )
                .data(),
            cc * cs,
            cr * cs
        );
        _enemyTex = GfxFactory::UploadTexture2D(
            MakeColorSheetPixels(
                cs,
                cc,
                cr,
                {
                    { 200, 60, 60 },
                    { 180, 50, 50 },
                    { 210, 70, 70 },
                    { 190, 55, 55 },
                    { 220, 80, 80 },
                    { 170, 45, 45 },
                    { 200, 65, 65 },
                    { 185, 55, 55 },
                }
            )
                .data(),
            cc * cs,
            cr * cs
        );
        const uint8_t npcPx[4] = { 180, 200, 80, 255 };
        _npcTex = GfxFactory::UploadTexture2D(npcPx, 1, 1);

        // Player data
        _player.x = 3 * ts;
        _player.y = 3 * ts;
        _player.w = _player.h = cs;
        _player.initSkills();
        _player.initItems();
        _playerAnimGO = CreateGameObject();
        _playerAnimGO->SetName("PlayerAnim");
        _playerFlip = static_cast<FlipbookComponent*>(_playerAnimGO->AddComponent<FlipbookComponent>());
        _playerFlip->SetWrapMode(WrapMode::Loop);
        _playerFlip->AddLocalClip(MakeClip("idle", 0, { 0, 1 }, 0.4f));
        _playerFlip->AddLocalClip(MakeClip("walk", 0, { 0, 1, 2, 3 }, 0.12f));
        _playerFlip->AddLocalClip(MakeClip("attack", 1, { 0, 1, 2 }, 0.1f));
        _playerFlip->Play("idle");

        // Enemies — define archetypes
        auto makeSlime = [&](float ex, float ey) {
            EnemyDef def;
            def.name = "Slime";
            def.stats = { 30, 30, 0, 0, 8, 5, 6 };
            def.expReward = 8;
            def.goldReward = 3;
            def.chooseAction = [](const Stats&, const Stats&) { return -1; };
            return Enemy(ex, ey, std::move(def));
        };
        auto makeGoblin = [&](float ex, float ey) {
            EnemyDef def;
            def.name = "Goblin";
            def.stats = { 45, 45, 0, 0, 12, 6, 10 };
            def.expReward = 12;
            def.goldReward = 5;
            def.chooseAction = [](const Stats&, const Stats&) { return -1; };
            return Enemy(ex, ey, std::move(def));
        };
        auto makeOrc = [&](float ex, float ey) {
            EnemyDef def;
            def.name = "Orc";
            def.stats = { 70, 70, 0, 0, 16, 10, 7 };
            def.expReward = 18;
            def.goldReward = 8;
            def.chooseAction = [](const Stats&, const Stats&) { return -1; };
            return Enemy(ex, ey, std::move(def));
        };

        const std::vector<std::pair<float, float>> spawns = {
            { 8 * ts, 8 * ts }, { 15 * ts, 12 * ts }, { 20 * ts, 6 * ts }, { 12 * ts, 16 * ts }, { 5 * ts, 14 * ts }
        };
        int idx = 0;
        for (auto [ex, ey] : spawns) {
            Enemy e = (idx % 3 == 0) ? makeSlime(ex, ey) : (idx % 3 == 1) ? makeGoblin(ex, ey) : makeOrc(ex, ey);
            e.w = e.h = cs;
            _enemies.push_back(std::move(e));
            idx++;// FlipbookComponents are created on the enemy AI GameObjects below
        }

        // NPCs
        _npcs.push_back(
            NPC(6 * ts,
                6 * ts,
                "Elder",
                {
                    "Elder: Walk into monsters to battle them!",
                    "Elder: Use Attack, Skills (costs MP) or Items.",
                    "Elder: Defeating foes grants EXP and gold.",
                    "Elder: Stay safe, adventurer!",
                })
        );
        _npcs.push_back(
            NPC(18 * ts,
                15 * ts,
                "Merchant",
                {
                    "Merchant: I trade in rare goods.",
                    "Merchant: (Shop not yet open)",
                })
        );

        // Lighting
        _lighting.ambientR = 0.25f;
        _lighting.ambientG = 0.28f;
        _lighting.ambientB = 0.40f;
        _lighting.ambientA = 1.0f;

        // ── Attach components ─────────────────────────────────────────────────

        // Battle system — owns all turn-based battle state and logic.
        auto* battleObj = CreateGameObject();
        battleObj->SetName("BattleSystem");
        battleObj->AddComponent<BattleSystemComponent>(
            &_player, _playerFlip, &_enemies, &_mode, &_transition, _fontID, _playerTex, _enemyTex, _screenW, _screenH
        );
        _battleComp = battleObj->GetComponent<BattleSystemComponent>();

        // Player movement component — updates _player.x/y each tick.
        auto* playerObj = CreateGameObject();
        playerObj->SetName("Player");
        playerObj->AddComponent<PlayerMovementComponent>(&_player, [this](float x, float y) {
            return _tilemap->IsSolidWorld(x, y);
        });
        // Inspector-only component: exposes player stats / progression in the editor.
        playerObj->AddComponent<PlayerComponent>(&_player, &_battleComp->GetBattle());

        // Enemy AI components — update _enemies[i].x/y/aggro each tick and
        // trigger battles on player contact.
        _enemyFlips.clear();
        for (size_t i = 0; i < _enemies.size(); i++) {
            auto* eObj = CreateGameObject();
            eObj->SetName("Enemy_" + std::to_string(i));

            auto* flip = static_cast<FlipbookComponent*>(eObj->AddComponent<FlipbookComponent>());
            flip->SetWrapMode(WrapMode::Loop);
            flip->AddLocalClip(MakeClip("idle", 0, { 0, 1 }, 0.6f));
            flip->AddLocalClip(MakeClip("walk", 0, { 0, 1, 2, 3 }, 0.15f));
            flip->Play("idle");
            _enemyFlips.push_back(flip);

            eObj->AddComponent<EnemyAIComponent>(
                &_enemies[i],
                flip,
                static_cast<int>(i),
                EnemyAICallbacks{
                    [this]() { return vec2(_player.cx(), _player.cy()); },
                    [this]() { return _player.aabb(); },
                    [this](float x, float y) { return _tilemap->IsSolidWorld(x, y); },
                    [this]() { return _mode == GameMode::Explore; },
                    [this](int k) {
                        if (_mode == GameMode::Explore) StartBattle(k);
                    },
                }
            );
        }
    }

    void OnUpdate(float dt, float /*time*/) override {
        _time += dt;
        _fadeIn = std::max(0.0f, _fadeIn - dt * 1.5f);

        if (_dialogTimer > 0) {
            _dialogTimer -= dt;
            if (_dialogTimer <= 0) _dialogText.clear();
        }

        switch (_mode) {
        case GameMode::Explore:
            UpdateExplore(dt);
            DrawExplore();
            DrawExploreHUD();
            if (!_dialogText.empty()) DrawDialog();
            break;

        case GameMode::BattleTransitionIn:
            _transition += dt * 3.0f;
            if (_transition >= 1.0f) {
                _transition = 1.0f;
                _mode = GameMode::Battle;
            }
            DrawExplore();
            GraphicsSubsystem::Get()->DrawQuad(
                0, 0, static_cast<float>(_screenW), static_cast<float>(_screenH), 0, vec4(0, 0, 0, _transition)
            );
            break;

        case GameMode::Battle:
            // UpdateBattle and DrawBattle are handled by BattleSystemComponent::OnTick.
            break;

        case GameMode::BattleTransitionOut:
            _transition -= dt * 3.0f;
            if (_transition <= 0.0f) {
                _transition = 0.0f;
                _mode = GameMode::Explore;
            }
            DrawExplore();
            GraphicsSubsystem::Get()->DrawQuad(
                0, 0, static_cast<float>(_screenW), static_cast<float>(_screenH), 0, vec4(0, 0, 0, _transition)
            );
            break;
        }

        if (_fadeIn > 0.01f)
            GraphicsSubsystem::Get()->DrawQuad(
                0, 0, static_cast<float>(_screenW), static_cast<float>(_screenH), 0, vec4(0, 0, 0, _fadeIn)
            );
    }

private:
    // Build a FlipbookClip from a row + column list of the 4x2 sprite sheet.
    // FromGrid bakes the same UVs the old col/row math produced (col/4, row/2).
    static FlipbookClip MakeClip(const std::string& name, int row, std::vector<int> cols, float dur) {
        std::vector<std::pair<int, int>> cells;
        cells.reserve(cols.size());
        for (int col : cols)
            cells.push_back({ col, row });
        return FlipbookClip::FromGrid(name, 4, 2, cells, dur);
    }

    void CameraFollow(float tcx, float tcy) {
        _camX = tcx - _screenW * 0.5f;
        _camY = tcy - _screenH * 0.5f;
    }
    void CameraClamp(int mapW, int mapH) {
        _camX = std::clamp(_camX, 0.0f, static_cast<float>(mapW - _screenW));
        _camY = std::clamp(_camY, 0.0f, static_cast<float>(mapH - _screenH));
    }

    void StartBattle(int enemyIdx) {
        if (_battleComp) _battleComp->StartBattle(enemyIdx);
    }

    void UpdateExplore(float dt) {
        auto* inp = InputSubsystem::Get();

        // Gameplay owns the animation state machine: switch clips only when the
        // desired state actually changes, not every frame. (Re-issuing Play every
        // frame would restart one-shots and fight a manual pause — that's the
        // caller's responsibility, not the engine's.) Frame advance itself is
        // driven centrally by AnimationSubsystem.
        const char* wantClip = _player.moving ? "walk" : "idle";
        if (_playerFlip->GetCurrentClip() != wantClip) _playerFlip->Play(wantClip);

        // NPC interaction
        if (inp->IsKeyPressed(Key::E)) {
            for (auto& npc : _npcs) {
                float dx = npc.cx() - _player.cx(), dy = npc.cy() - _player.cy();
                if (std::sqrt(dx * dx + dy * dy) < npc.talkR) {
                    _dialogText = npc.dialogue[npc.dialogIdx % static_cast<int>(npc.dialogue.size())];
                    npc.dialogIdx++;
                    _dialogTimer = 3.5f;
                    break;
                }
            }
        }

        CameraFollow(_player.cx(), _player.cy());
        CameraClamp(_tilemap->GetPixelWidth(), _tilemap->GetPixelHeight());

        _lighting.lights.clear();
        _lighting.lights.push_back({ _player.cx() - _camX, _player.cy() - _camY, 1.0f, 0.9f, 0.7f, 120.0f, 1.5f });
        for (const auto& e : _enemies) {
            if (!e.alive || !e.aggro) continue;
            float dx = e.cx() - _player.cx(), dy = e.cy() - _player.cy();
            if (dx * dx + dy * dy > 250.0f * 250.0f) continue;
            _lighting.lights.push_back({ e.cx() - _camX, e.cy() - _camY, 1, 0.3f, 0.2f, 80, 0.7f });
        }
    }

    void DrawExplore() {
        auto* gfx = GraphicsSubsystem::Get();
        float camX = _camX, camY = _camY;

        _tilemap->Draw(gfx, camX, camY, _screenW, _screenH);

        for (const auto& npc : _npcs) {
            float sx = npc.x - camX, sy = npc.y - camY;
            gfx->DrawTexturedQuad(sx + npc.w * 0.5f, sy + npc.h * 0.5f, npc.w, npc.h, 0, _npcTex, vec4(1));
        }

        constexpr int ccols = 4, crows = 2;
        for (size_t i = 0; i < _enemies.size(); i++) {
            if (!_enemies[i].alive) continue;
            const Enemy& e = _enemies[i];
            float sx = e.x - camX, sy = e.y - camY;
            vec2 uv0{ 0, 0 }, uv1{ 1.0f / ccols, 1.0f / crows };
            _enemyFlips[i]->GetCurrentUV(uv0, uv1);
            gfx->DrawSprite2D(sx, sy, e.w, e.h, _enemyTex, uv0, uv1);
        }

        {
            float sx = _player.x - camX, sy = _player.y - camY;
            vec2 uv0{ 0, 0 }, uv1{ 1.0f / ccols, 1.0f / crows };
            _playerFlip->GetCurrentUV(uv0, uv1);
            gfx->DrawSprite2D(sx, sy, _player.w, _player.h, _playerTex, uv0, uv1);
        }

        _lighting.Apply(gfx, _screenW, _screenH);
    }

    void DrawExploreHUD() {
        auto* gfx = GraphicsSubsystem::Get();
        const Stats& s = _player.stats;
        const auto& b = _battleComp->GetBattle();

        DrawPanel(8, 8, 160, 50);
        gfx->DrawText(
            _fontID,
            fmt::format("LV{} EXP:{}/{}", b.level, b.exp, b.expToNext),
            14,
            12,
            0.5f,
            vec4(0.9f, 0.85f, 0.6f, 1)
        );
        DrawHPBar(14, 28, 140, 10, s.hp, s.maxHp);
        DrawMPBar(14, 42, 140, 8, s.mp, s.maxMp);

        gfx->DrawText(
            _fontID,
            fmt::format("Gold:{}", b.gold),
            14,
            static_cast<float>(_screenH) - 36,
            0.55f,
            vec4(1, 0.9f, 0.3f, 1)
        );
        gfx->DrawText(
            _fontID,
            "WASD:move  E:talk  walk into enemy to battle",
            14,
            static_cast<float>(_screenH) - 20,
            0.45f,
            vec4(0.7f, 0.7f, 0.7f, 0.8f)
        );
    }

    void DrawDialog() {
        auto* gfx = GraphicsSubsystem::Get();
        const auto w = static_cast<float>(_screenW), h = static_cast<float>(_screenH);
        const float bx = 40, bh = 70, by = h - bh - 20, bw = w - 80;
        DrawPanel(bx, by, bw, bh);
        gfx->DrawQuad(bx + bw * 0.5f, by + 1.5f, bw, 3, 0, vec4(0.5f, 0.8f, 1, 1));
        if (_fontID) gfx->DrawText(_fontID, _dialogText, bx + 12, by + 14, 0.7f, vec4(0.9f, 0.9f, 1, 1));
    }

    void DrawPanel(float x, float y, float w, float h, vec4 bg = vec4(0.05f, 0.05f, 0.12f, 0.92f)) {
        auto* gfx = GraphicsSubsystem::Get();
        gfx->DrawQuad(x + w * 0.5f, y + h * 0.5f, w, h, 0, bg);
        gfx->DrawRect(x, y, w, h, vec4(0.35f, 0.35f, 0.55f, 0.8f));
    }

    void DrawHPBar(float x, float y, float w, float h, int hp, int maxHp, vec4 color = vec4(0.2f, 0.85f, 0.2f, 1)) {
        auto* gfx = GraphicsSubsystem::Get();
        gfx->DrawQuad(x + w * 0.5f, y + h * 0.5f, w, h, 0, vec4(0.1f, 0.1f, 0.1f, 0.85f));
        float ratio = maxHp > 0 ? static_cast<float>(hp) / maxHp : 0;
        float fw = (w - 2) * ratio;
        if (fw > 0) gfx->DrawQuad(x + 1 + fw * 0.5f, y + 1 + (h - 2) * 0.5f, fw, h - 2, 0, color);
    }

    void DrawMPBar(float x, float y, float w, float h, int mp, int maxMp) {
        auto* gfx = GraphicsSubsystem::Get();
        gfx->DrawQuad(x + w * 0.5f, y + h * 0.5f, w, h, 0, vec4(0.1f, 0.1f, 0.1f, 0.85f));
        float ratio = maxMp > 0 ? static_cast<float>(mp) / maxMp : 0;
        float fw = (w - 2) * ratio;
        if (fw > 0) gfx->DrawQuad(x + 1 + fw * 0.5f, y + 1 + (h - 2) * 0.5f, fw, h - 2, 0, vec4(0.2f, 0.4f, 1, 1));
    }
};

int main() {
    RPGGame game;
    game.Run();
    return 0;
}

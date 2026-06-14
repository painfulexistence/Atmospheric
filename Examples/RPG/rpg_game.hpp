#pragma once
#include <Atmospheric/application.hpp>
#include <Atmospheric/tilemap_2d.hpp>
#include <Atmospheric/lighting_2d.hpp>
#include "rpg_entity.hpp"
#include <memory>
#include <string>
#include <vector>

enum class GameMode { Explore, BattleTransitionIn, Battle, BattleTransitionOut };

class RPGGame : public Application {
public:
    RPGGame();
    void OnInit()  override;
    void OnLoad()  override;
    void OnUpdate(float dt, float time) override;

private:
    // ── resources ──────────────────────────────────────────────────────
    uint32_t _tilesetTex = 0, _playerTex = 0, _enemyTex = 0, _npcTex = 0;
    FontID   _fontID = 0;

    std::unique_ptr<Tilemap2D> _tilemap;
    LightingSystem2D           _lighting;

    // ── world entities ─────────────────────────────────────────────────
    Player               _player;
    SpriteAnimator       _playerAnim;
    std::vector<Enemy>         _enemies;
    std::vector<SpriteAnimator> _enemyAnims;
    std::vector<NPC>           _npcs;

    // ── camera ─────────────────────────────────────────────────────────
    float _camX = 0, _camY = 0;
    int   _screenW = 800, _screenH = 600;
    void  CameraFollow(float cx, float cy);
    void  CameraClamp(int mapW, int mapH);

    // ── game state ─────────────────────────────────────────────────────
    GameMode    _mode     = GameMode::Explore;
    float       _fadeIn   = 1.0f;
    float       _transition = 0.0f;  // 0→1 for battle transition
    std::string _dialogText;
    float       _dialogTimer = 0.0f;
    float       _time = 0.0f;

    // ── battle ─────────────────────────────────────────────────────────
    BattleState _battle;
    float       _shakeMag = 0.0f;    // screen shake
    float       _shakeTimer = 0.0f;

    void StartBattle(int enemyIdx);
    void EndBattle(bool victory);

    // Exploration update/draw
    void UpdateExplore(float dt);
    void DrawExplore();
    void DrawExploreHUD();
    void DrawDialog();

    // Battle update/draw
    void UpdateBattle(float dt);
    void DrawBattle();
    void DrawBattleMenu();
    void DrawBattleLog();
    void DrawBattleStats();

    // Battle actions
    void ExecutePlayerAttack();
    void ExecutePlayerSkill(int skillIdx);
    void ExecutePlayerItem(int itemIdx);
    void ExecuteEnemyTurn();
    int  CalcDamage(int atk, int def);

    // Helpers
    void DrawHPBar(float x, float y, float w, float h, int hp, int maxHp,
                   glm::vec4 color = glm::vec4(0.2f,0.85f,0.2f,1));
    void DrawMPBar(float x, float y, float w, float h, int mp, int maxMp);
    void DrawPanel(float x, float y, float w, float h,
                   glm::vec4 bg = glm::vec4(0.05f,0.05f,0.12f,0.92f));

    static AnimClip MakeClip(int row, std::vector<int> cols, float dur, bool loop=true);

    std::mt19937 _rng{std::random_device{}()};
};

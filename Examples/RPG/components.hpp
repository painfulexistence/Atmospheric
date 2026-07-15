#pragma once
#include "Atmospheric.hpp"
#include "battle_ui_page.hpp"
#include "imgui.h"
#include "rpg_entity.hpp"
#include "ui_kit.hpp"
#include <cmath>
#include <fmt/format.h>
#include <functional>
#include <vector>

using glm::vec2;

// Handles WASD movement with axis-separated tilemap collision.
class PlayerMovementComponent : public Component {
    Player* _player;
    std::function<bool(float, float)> _isSolid;

public:
    PlayerMovementComponent(GameObject* go, Player* player, std::function<bool(float, float)> isSolid)
      : _player(player), _isSolid(std::move(isSolid)) {
    }

    std::string GetName() const override {
        return "PlayerMovementComponent";
    }

    void OnTick(float dt) override {
        auto* inp = InputSubsystem::Get();
        float ax = 0, ay = 0;
        if (inp->IsKeyDown(Key::LEFT) || inp->IsKeyDown(Key::A)) ax -= 1;
        if (inp->IsKeyDown(Key::RIGHT) || inp->IsKeyDown(Key::D)) ax += 1;
        if (inp->IsKeyDown(Key::UP) || inp->IsKeyDown(Key::W)) ay -= 1;
        if (inp->IsKeyDown(Key::DOWN) || inp->IsKeyDown(Key::S)) ay += 1;

        Player& p = *_player;
        p.moving = (ax || ay);
        if (!p.moving) return;

        float len = std::sqrt(ax * ax + ay * ay);
        float nx = ax / len, ny = ay / len;
        float newX = p.x + nx * p.speed * dt;
        float newY = p.y + ny * p.speed * dt;

        if (!_isSolid(newX, p.y) && !_isSolid(newX + p.w - 1, p.y) && !_isSolid(newX, p.y + p.h - 1)
            && !_isSolid(newX + p.w - 1, p.y + p.h - 1))
            p.x = newX;
        if (!_isSolid(p.x, newY) && !_isSolid(p.x + p.w - 1, newY) && !_isSolid(p.x, p.y + p.h - 1)
            && !_isSolid(p.x + p.w - 1, p.y + p.h - 1))
            p.y = newY;

        if (ax) p.facing = (ax > 0) ? 1.0f : -1.0f;
    }

    void DrawImGui() override {
        ImGui::DragFloat("Move speed", &_player->speed, 1.0f, 10.0f, 500.0f);
        ImGui::Text("Pos: %.1f, %.1f", _player->x, _player->y);
        ImGui::Text("Facing: %s", _player->facing > 0 ? "right" : "left");
    }
};

// Inspector-only: exposes Player stats and progression in the editor.
class PlayerComponent : public Component {
    Player* _player;
    BattleState* _battle;

public:
    PlayerComponent(GameObject* go, Player* player, BattleState* battle) : _player(player), _battle(battle) {
    }

    std::string GetName() const override {
        return "PlayerComponent";
    }

    void DrawImGui() override {
        Stats& s = _player->stats;
        ImGui::Text("Level: %d   EXP: %d/%d", _battle->level, _battle->exp, _battle->expToNext);
        ImGui::DragInt("Gold", &_battle->gold, 1.0f, 0, 999999);
        ImGui::Separator();
        ImGui::DragInt("HP", &s.hp, 1.0f, 0, s.maxHp);
        ImGui::DragInt("Max HP", &s.maxHp, 1.0f, 1, 9999);
        ImGui::DragInt("MP", &s.mp, 1.0f, 0, s.maxMp);
        ImGui::DragInt("Max MP", &s.maxMp, 1.0f, 0, 9999);
        ImGui::DragInt("ATK", &s.atk, 1.0f, 0, 9999);
        ImGui::DragInt("DEF", &s.def, 1.0f, 0, 9999);
        ImGui::DragInt("SPD", &s.spd, 1.0f, 0, 9999);
    }
};

struct EnemyAICallbacks {
    std::function<vec2()> getPlayerCenter;
    std::function<Rect()> getPlayerRect;
    std::function<bool(float, float)> isSolid;
    std::function<bool()> isExploring;
    std::function<void(int)> onContact;
};

// Aggro detection, movement, animation, and battle trigger for a single enemy.
class EnemyAIComponent : public Component {
    Enemy* _data;
    FlipbookComponent* _anim;
    int _idx;
    EnemyAICallbacks _cb;

public:
    EnemyAIComponent(GameObject* go, Enemy* data, FlipbookComponent* anim, int idx, EnemyAICallbacks cbs)
      : _data(data), _anim(anim), _idx(idx), _cb(std::move(cbs)) {
    }

    std::string GetName() const override {
        return fmt::format("EnemyAI [{}]", _data->def.name);
    }

    void OnTick(float dt) override {
        if (!_data->alive || !_cb.isExploring()) return;

        vec2 pc = _cb.getPlayerCenter();
        float dx = pc.x - _data->cx(), dy = pc.y - _data->cy();
        float dist = std::sqrt(dx * dx + dy * dy);

        if (dist < _data->aggroR) {
            _data->aggro = true;
            float nx = dx / dist, ny = dy / dist;
            float newX = _data->x + nx * 30.0f * dt;
            float newY = _data->y + ny * 30.0f * dt;

            if (!_cb.isSolid(newX, _data->y) && !_cb.isSolid(newX + _data->w - 1, _data->y)
                && !_cb.isSolid(newX, _data->y + _data->h - 1)
                && !_cb.isSolid(newX + _data->w - 1, _data->y + _data->h - 1))
                _data->x = newX;
            if (!_cb.isSolid(_data->x, newY) && !_cb.isSolid(_data->x + _data->w - 1, newY)
                && !_cb.isSolid(_data->x, _data->y + _data->h - 1)
                && !_cb.isSolid(_data->x + _data->w - 1, _data->y + _data->h - 1))
                _data->y = newY;
        } else {
            _data->aggro = false;
        }

        // Switch clips only on a state change — the AI decides when, the engine
        // plays it. Frame advance is driven centrally by AnimationSubsystem.
        const char* wantClip = _data->aggro ? "walk" : "idle";
        if (_anim->GetCurrentClip() != wantClip) _anim->Play(wantClip);

        if (RectOverlaps(_cb.getPlayerRect(), _data->rect())) {
            _cb.onContact(_idx);
        }
    }

    void DrawImGui() override {
        ImGui::Text("Alive: %s   Aggro: %s", _data->alive ? "yes" : "no", _data->aggro ? "yes" : "no");
        ImGui::Text("Pos: %.1f, %.1f", _data->x, _data->y);
        ImGui::DragFloat("Aggro radius", &_data->aggroR, 1.0f, 0.0f, 400.0f);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// BattleScene — the *world-coupled* half of the battle screen, on the Canvas
// layer: an opaque backdrop, scanlines, and the combatant sprites. Sprites need
// per-frame UVs (from the shared flipbook), tint and shake, so they belong here
// rather than in the RmlUi document (which owns the text/menus/bars instead).
// ─────────────────────────────────────────────────────────────────────────────
struct BattleScene {
    static constexpr int MAX_ENEMIES = BattleUIPage::MAX_ENEMIES;
    static constexpr int CCOLS = 4, CROWS = 2;

    rpgui::Quad _backdrop;
    std::vector<rpgui::Quad> _scanlines;
    rpgui::Sprite _playerActor;
    rpgui::Sprite _enemyActors[MAX_ENEMIES];
    float _W = 800, _H = 600;

    void Build(Application* app, TextureHandle white, TextureHandle playerTex, TextureHandle enemyTex, int screenW, int screenH) {
        _W = static_cast<float>(screenW);
        _H = static_cast<float>(screenH);
        const float W = _W, H = _H;

        _backdrop.Build(app, white, { 0, 0 }, { W, H }, glm::vec4(0.08f, 0.06f, 0.14f, 1), rpgui::Z_BACKDROP);
        _scanlines.resize(8);
        for (int i = 0; i < 8; i++)
            _scanlines[i].Build(
                app, white, { 0, i * H / 8.0f }, { W, 1 }, glm::vec4(0.15f, 0.12f, 0.22f, 0.5f), rpgui::Z_SCANLINE
            );

        _playerActor.Build(app, playerTex, { W * 0.25f, H * 0.42f }, { 72, 72 });
        for (int i = 0; i < MAX_ENEMIES; i++) {
            _enemyActors[i].Build(app, enemyTex, { W * 0.65f + i * 100, H * 0.35f }, { 72, 72 });
            _enemyActors[i].SetUVs({ 0, 0 }, { 1.0f / CCOLS, 1.0f / CROWS });
        }
        Hide();
    }

    void Hide() {
        _backdrop.SetActive(false);
        for (auto& s : _scanlines) s.SetActive(false);
        _playerActor.SetActive(false);
        for (auto& e : _enemyActors) e.SetActive(false);
    }

    void Sync(const BattleState& b, FlipbookComponent* playerAnim, float shakeX, float shakeY) {
        const float W = _W, H = _H;
        _backdrop.SetActive(true);
        for (auto& s : _scanlines) s.SetActive(true);

        _playerActor.SetActive(true);
        _playerActor.SetCenter({ W * 0.25f, H * 0.42f });
        glm::vec2 uv0{ 0, 0 }, uv1{ 1.0f / CCOLS, 1.0f / CROWS };
        if (playerAnim) playerAnim->GetCurrentUV(uv0, uv1);
        _playerActor.SetUVs(uv0, uv1);

        int count = static_cast<int>(b.enemyStats.size());
        for (int i = 0; i < MAX_ENEMIES; i++) {
            bool shown = i < count && !b.enemyStats[i].isDead();
            _enemyActors[i].SetActive(shown);
            if (!shown) continue;
            _enemyActors[i].SetCenter({ W * 0.65f + i * 100 + shakeX, H * 0.35f + shakeY });
            _enemyActors[i].SetColor((b.targetIdx == i) ? glm::vec4(1, 0.7f, 0.7f, 1) : glm::vec4(1));
        }
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// BattleSystemComponent — owns all turn-based battle state and logic, and drives
// the two halves of the battle view: the Canvas BattleScene (sprites/backdrop)
// and the RmlUi BattleUIPage (menus/log/stats/enemy read-outs). No rendering is
// done by hand any more: OnTick advances state and reconciles it onto both.
// ─────────────────────────────────────────────────────────────────────────────
class BattleSystemComponent : public Component {
    Player* _player;
    FlipbookComponent* _playerAnim;
    std::vector<Enemy>* _enemies;
    GameMode* _mode;
    float* _transition;
    FontHandle _fontID;
    TextureHandle _whiteTex, _playerTex, _enemyTex;
    int _screenW, _screenH;

    BattleState _battle;
    BattleScene _scene;
    BattleUIPage* _ui = nullptr;
    float _shakeMag = 0;
    float _shakeTimer = 0;
    float _shakeX = 0, _shakeY = 0;
    std::mt19937 _rng{ std::random_device{}() };

public:
    BattleSystemComponent(
        GameObject* go,
        Player* player,
        FlipbookComponent* playerAnim,
        std::vector<Enemy>* enemies,
        GameMode* mode,
        float* transition,
        FontHandle fontID,
        TextureHandle whiteTex,
        TextureHandle playerTex,
        TextureHandle enemyTex,
        int screenW,
        int screenH
    )
      : _player(player), _playerAnim(playerAnim), _enemies(enemies), _mode(mode), _transition(transition),
        _fontID(fontID), _whiteTex(whiteTex), _playerTex(playerTex), _enemyTex(enemyTex), _screenW(screenW),
        _screenH(screenH) {
        gameObject = go;
        _battle.playerStats = &_player->stats;
    }

    std::string GetName() const override {
        return "BattleSystemComponent";
    }

    void OnAttach() override {
        // Canvas half: backdrop + combatant sprites (starts hidden).
        _scene.Build(gameObject->GetApp(), _whiteTex, _playerTex, _enemyTex, _screenW, _screenH);
        // RmlUi half: menus / log / stats, on its own GameObject via UIPageComponent.
        _ui = static_cast<BattleUIPage*>(
            gameObject->GetApp()->CreateGameObject()->AddComponent<BattleUIPage>("assets/ui/battle.rml")
        );
    }

    BattleState& GetBattle() {
        return _battle;
    }

    void StartBattle(int enemyIdx) {
        *_mode = GameMode::BattleTransitionIn;
        *_transition = 0.0f;

        Enemy& e = (*_enemies)[enemyIdx];
        e.resetBattleStats();

        _battle.phase = BattlePhase::PlayerMenu;
        _battle.menuSel = 0;
        _battle.skillSel = 0;
        _battle.itemSel = 0;
        _battle.targetIdx = 0;
        _battle.actionTimer = 0.0f;
        _battle.log.clear();
        _battle.expGained = 0;
        _battle.goldGained = 0;

        _battle.enemyStats.clear();
        _battle.enemyNames.clear();
        _battle.enemyIndices.clear();
        _battle.enemyStats.push_back(e.battle);
        _battle.enemyNames.push_back(e.def.name);
        _battle.enemyIndices.push_back(enemyIdx);

        float dx = e.cx() - _player->cx(), dy = e.cy() - _player->cy();
        float len = std::sqrt(dx * dx + dy * dy);
        if (len > 0) {
            e.x += dx / len * 32;
            e.y += dy / len * 32;
        }

        _battle.pushLog(fmt::format("A {} appeared!", e.def.name));
        _battle.playerFirst = (_player->stats.spd >= e.def.stats.spd);
    }

    void OnTick(float dt) override {
        // Battle view (Canvas scene + RmlUi UI) is live only during the Battle
        // mode itself, not during the fade transitions — mirroring how the old
        // DrawBattle only ran in GameMode::Battle.
        if (*_mode != GameMode::Battle) return;
        if (_ui && !_ui->IsVisible()) _ui->Show();
        UpdateBattle(dt);
        _scene.Sync(_battle, _playerAnim, _shakeX, _shakeY);
        if (_ui) _ui->Sync(_battle, *_player);
    }

    void DrawImGui() override {
        ImGui::Text("Phase: %d", static_cast<int>(_battle.phase));
        ImGui::Text("Level: %d  EXP: %d/%d  Gold: %d", _battle.level, _battle.exp, _battle.expToNext, _battle.gold);
        ImGui::Text("Shake: %.2f (%.2fs)", _shakeMag, _shakeTimer);
    }

private:
    void EndBattle(bool victory) {
        if (victory) {
            _battle.exp += _battle.expGained;
            _battle.gold += _battle.goldGained;
            while (_battle.exp >= _battle.expToNext) {
                _battle.exp -= _battle.expToNext;
                _battle.level++;
                _battle.expToNext = static_cast<int>(_battle.expToNext * 1.5f);
                _player->stats.maxHp += 8;
                _player->stats.maxMp += 4;
                _player->stats.atk += 2;
                _player->stats.def += 1;
                _player->stats.spd += 1;
                _player->stats.hp = _player->stats.maxHp;
                _player->stats.mp = _player->stats.maxMp;
                _battle.pushLog(fmt::format("Level up! Now LV{}!", _battle.level));
            }
            for (int idx : _battle.enemyIndices)
                if (idx >= 0 && idx < static_cast<int>(_enemies->size())) (*_enemies)[idx].alive = false;
        }
        _scene.Hide();
        if (_ui) _ui->Hide();
        *_mode = GameMode::BattleTransitionOut;
        *_transition = 1.0f;
    }

    int CalcDamage(int atk, int def) {
        std::uniform_int_distribution<int> jitter(-3, 3);
        return std::max(1, atk - def / 2 + jitter(_rng));
    }

    void ExecutePlayerAttack() {
        Stats& es = _battle.enemyStats[_battle.targetIdx];
        int dmg = CalcDamage(_player->stats.atk, es.def);
        es.takeDamage(dmg);
        _shakeMag = 4.0f;
        _shakeTimer = 0.3f;
        _battle.pushLog(fmt::format("You attack {} for {} dmg!", _battle.enemyNames[_battle.targetIdx], dmg));
        _battle.phase = BattlePhase::EnemyAction;
        _battle.actionTimer = 0.8f;
    }

    void ExecutePlayerSkill(int idx) {
        if (idx >= static_cast<int>(_player->skills.size())) return;
        Skill& sk = _player->skills[idx];
        if (_player->stats.mp < sk.mpCost) {
            _battle.pushLog("Not enough MP!");
            _battle.phase = BattlePhase::PlayerMenu;
            return;
        }
        _player->stats.mp -= sk.mpCost;
        if (sk.target == SkillTarget::AllEnemies) {
            int total = 0;
            for (auto& es : _battle.enemyStats) {
                if (es.isDead()) continue;
                int v = sk.calc(_player->stats, es);
                es.takeDamage(v);
                total += v;
            }
            _battle.pushLog(
                fmt::format("{} hits all for ~{} dmg!", sk.name, total / static_cast<int>(_battle.enemyStats.size()))
            );
        } else if (sk.target == SkillTarget::Self) {
            int v = sk.calc(_player->stats, _player->stats);
            _player->stats.heal(-v);
            _battle.pushLog(fmt::format("{}: restored {} HP!", sk.name, -v));
        } else {
            Stats& es = _battle.enemyStats[_battle.targetIdx];
            int v = sk.calc(_player->stats, es);
            es.takeDamage(v);
            _battle.pushLog(fmt::format("{} deals {} dmg!", sk.name, v));
        }
        _shakeMag = 6.0f;
        _shakeTimer = 0.3f;
        _battle.phase = BattlePhase::EnemyAction;
        _battle.actionTimer = 0.8f;
    }

    void ExecutePlayerItem(int idx) {
        if (idx >= static_cast<int>(_player->items.size())) return;
        Item& it = _player->items[idx];
        if (it.count <= 0) {
            _battle.pushLog("None left!");
            return;
        }
        it.count--;
        if (it.effect == ItemEffect::HealHP) {
            _player->stats.heal(it.amount);
            _battle.pushLog(fmt::format("{}: recovered {} HP!", it.name, it.amount));
        } else {
            _player->stats.restoreMp(it.amount);
            _battle.pushLog(fmt::format("{}: recovered {} MP!", it.name, it.amount));
        }
        _battle.phase = BattlePhase::EnemyAction;
        _battle.actionTimer = 0.8f;
    }

    void ExecuteEnemyTurn() {
        int actorIdx = -1;
        for (int i = 0; i < static_cast<int>(_battle.enemyStats.size()); i++)
            if (!_battle.enemyStats[i].isDead()) {
                actorIdx = i;
                break;
            }
        if (actorIdx < 0) return;
        const Enemy& worldE = (*_enemies)[_battle.enemyIndices[actorIdx]];
        int dmg = CalcDamage(worldE.def.stats.atk, _player->stats.def);
        _player->stats.takeDamage(dmg);
        _battle.pushLog(fmt::format("{} attacks for {} dmg!", worldE.def.name, dmg));
        _battle.phase = BattlePhase::PlayerMenu;
    }

    void UpdateBattle(float dt) {
        auto* inp = InputSubsystem::Get();
        BattleState& b = _battle;
        if (_shakeTimer > 0) {
            _shakeTimer -= dt;
            std::uniform_real_distribution<float> d(-_shakeMag, _shakeMag);
            _shakeX = d(_rng);
            _shakeY = d(_rng);
        } else {
            _shakeMag = 0;
            _shakeX = _shakeY = 0;
        }
        b.tickLog(dt);
        switch (b.phase) {
        case BattlePhase::PlayerMenu: {
            int count = static_cast<int>(BattleMenuSel::COUNT);
            if (inp->IsKeyPressed(Key::UP) || inp->IsKeyPressed(Key::W)) b.menuSel = (b.menuSel + count - 1) % count;
            if (inp->IsKeyPressed(Key::DOWN) || inp->IsKeyPressed(Key::S)) b.menuSel = (b.menuSel + 1) % count;
            if (inp->IsKeyPressed(Key::Z) || inp->IsKeyPressed(Key::ENTER)) {
                switch ((BattleMenuSel)b.menuSel) {
                case BattleMenuSel::Attack:
                    ExecutePlayerAttack();
                    break;
                case BattleMenuSel::Skills:
                    b.phase = BattlePhase::PlayerSkillMenu;
                    b.skillSel = 0;
                    break;
                case BattleMenuSel::Items:
                    b.phase = BattlePhase::PlayerItemMenu;
                    b.itemSel = 0;
                    break;
                case BattleMenuSel::Run:
                    b.phase = BattlePhase::Flee;
                    b.actionTimer = 0.5f;
                    break;
                default:
                    break;
                }
            }
            break;
        }
        case BattlePhase::PlayerSkillMenu: {
            int count = static_cast<int>(_player->skills.size());
            if (inp->IsKeyPressed(Key::UP) || inp->IsKeyPressed(Key::W)) b.skillSel = (b.skillSel + count - 1) % count;
            if (inp->IsKeyPressed(Key::DOWN) || inp->IsKeyPressed(Key::S)) b.skillSel = (b.skillSel + 1) % count;
            if (inp->IsKeyPressed(Key::Z) || inp->IsKeyPressed(Key::ENTER)) ExecutePlayerSkill(b.skillSel);
            if (inp->IsKeyPressed(Key::X) || inp->IsKeyPressed(Key::ESCAPE)) b.phase = BattlePhase::PlayerMenu;
            break;
        }
        case BattlePhase::PlayerItemMenu: {
            int count = static_cast<int>(_player->items.size());
            if (inp->IsKeyPressed(Key::UP) || inp->IsKeyPressed(Key::W)) b.itemSel = (b.itemSel + count - 1) % count;
            if (inp->IsKeyPressed(Key::DOWN) || inp->IsKeyPressed(Key::S)) b.itemSel = (b.itemSel + 1) % count;
            if (inp->IsKeyPressed(Key::Z) || inp->IsKeyPressed(Key::ENTER)) ExecutePlayerItem(b.itemSel);
            if (inp->IsKeyPressed(Key::X) || inp->IsKeyPressed(Key::ESCAPE)) b.phase = BattlePhase::PlayerMenu;
            break;
        }
        case BattlePhase::EnemyAction:
            b.actionTimer -= dt;
            if (b.actionTimer <= 0) {
                if (b.allEnemiesDead()) {
                    for (int idx : b.enemyIndices)
                        if (idx < static_cast<int>(_enemies->size())) {
                            b.expGained += (*_enemies)[idx].def.expReward;
                            b.goldGained += (*_enemies)[idx].def.goldReward;
                        }
                    b.pushLog(fmt::format("Victory! +{} EXP +{} Gold", b.expGained, b.goldGained));
                    b.phase = BattlePhase::Victory;
                    b.actionTimer = 2.5f;
                } else if (_player->stats.isDead()) {
                    b.pushLog("You were defeated...");
                    b.phase = BattlePhase::Defeat;
                    b.actionTimer = 2.5f;
                } else {
                    ExecuteEnemyTurn();
                }
            }
            break;
        case BattlePhase::Victory:
            b.actionTimer -= dt;
            if (b.actionTimer <= 0) EndBattle(true);
            break;
        case BattlePhase::Defeat:
            b.actionTimer -= dt;
            if (b.actionTimer <= 0) {
                _player->stats.hp = 1;
                EndBattle(false);
            }
            break;
        case BattlePhase::Flee:
            b.actionTimer -= dt;
            if (b.actionTimer <= 0) {
                b.pushLog("Escaped!");
                EndBattle(false);
            }
            break;
        default:
            break;
        }
    }
};

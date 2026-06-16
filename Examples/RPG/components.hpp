#pragma once
#include "rpg_game.hpp"
#include <Atmospheric/component.hpp>
#include <Atmospheric/input.hpp>
#include <cmath>
#include <functional>
#include <fmt/format.h>
#include "imgui.h"

using glm::vec2;

// Handles WASD movement with axis-separated tilemap collision.
class PlayerMovementComponent : public Component {
    Player* _player;
    std::function<bool(float, float)> _isSolid;
public:
    PlayerMovementComponent(GameObject* go, Player* player,
                            std::function<bool(float, float)> isSolid)
        : _player(player), _isSolid(std::move(isSolid)) {}

    std::string GetName() const override { return "PlayerMovementComponent"; }

    void OnTick(float dt) override {
        auto* inp = gameObject->GetApp()->GetInput();
        float ax = 0, ay = 0;
        if (inp->IsKeyDown(Key::LEFT)  || inp->IsKeyDown(Key::A)) ax -= 1;
        if (inp->IsKeyDown(Key::RIGHT) || inp->IsKeyDown(Key::D)) ax += 1;
        if (inp->IsKeyDown(Key::UP)    || inp->IsKeyDown(Key::W)) ay -= 1;
        if (inp->IsKeyDown(Key::DOWN)  || inp->IsKeyDown(Key::S)) ay += 1;

        Player& p = *_player;
        p.moving = (ax || ay);
        if (!p.moving) return;

        float len = std::sqrt(ax*ax + ay*ay);
        float nx = ax/len, ny = ay/len;
        float newX = p.x + nx * p.speed * dt;
        float newY = p.y + ny * p.speed * dt;

        if (!_isSolid(newX, p.y)         && !_isSolid(newX + p.w - 1, p.y) &&
            !_isSolid(newX, p.y + p.h-1) && !_isSolid(newX + p.w-1, p.y + p.h-1))
            p.x = newX;
        if (!_isSolid(p.x, newY)         && !_isSolid(p.x + p.w - 1, newY) &&
            !_isSolid(p.x, newY + p.h-1) && !_isSolid(p.x + p.w-1, p.y + p.h-1))
            p.y = newY;

        if (ax) p.facing = (ax > 0) ? 1.0f : -1.0f;
    }

    void DrawImGui() override {
        if (ImGui::CollapsingHeader("PlayerMovementComponent")) {
            ImGui::DragFloat("Move speed", &_player->speed, 1.0f, 10.0f, 500.0f);
            ImGui::Text("Pos: %.1f, %.1f", _player->x, _player->y);
            ImGui::Text("Facing: %s", _player->facing > 0 ? "right" : "left");
        }
    }
};

// Inspector-only: exposes Player stats and progression in the editor.
class PlayerComponent : public Component {
    Player*      _player;
    BattleState* _battle;
public:
    PlayerComponent(GameObject* go, Player* player, BattleState* battle)
        : _player(player), _battle(battle) {}

    std::string GetName() const override { return "PlayerComponent"; }

    void DrawImGui() override {
        if (ImGui::CollapsingHeader("PlayerComponent", ImGuiTreeNodeFlags_DefaultOpen)) {
            Stats& s = _player->stats;
            ImGui::Text("Level: %d   EXP: %d/%d", _battle->level, _battle->exp, _battle->expToNext);
            ImGui::DragInt("Gold", &_battle->gold, 1.0f, 0, 999999);
            ImGui::Separator();
            ImGui::DragInt("HP",     &s.hp,    1.0f, 0, s.maxHp);
            ImGui::DragInt("Max HP", &s.maxHp, 1.0f, 1, 9999);
            ImGui::DragInt("MP",     &s.mp,    1.0f, 0, s.maxMp);
            ImGui::DragInt("Max MP", &s.maxMp, 1.0f, 0, 9999);
            ImGui::DragInt("ATK",    &s.atk,   1.0f, 0, 9999);
            ImGui::DragInt("DEF",    &s.def,   1.0f, 0, 9999);
            ImGui::DragInt("SPD",    &s.spd,   1.0f, 0, 9999);
        }
    }
};

struct EnemyAICallbacks {
    std::function<vec2()>              getPlayerCenter;
    std::function<AABB()>              getPlayerAABB;
    std::function<bool(float, float)>  isSolid;
    std::function<bool()>              isExploring;
    std::function<void(int)>           onContact;
};

// Aggro detection, movement, animation, and battle trigger for a single enemy.
class EnemyAIComponent : public Component {
    Enemy*          _data;
    SpriteAnimator* _anim;
    int             _idx;
    EnemyAICallbacks _cb;
public:
    EnemyAIComponent(GameObject* go, Enemy* data, SpriteAnimator* anim,
                     int idx, EnemyAICallbacks cbs)
        : _data(data), _anim(anim), _idx(idx), _cb(std::move(cbs)) {}

    std::string GetName() const override { return "EnemyAIComponent"; }

    void OnTick(float dt) override {
        if (!_data->alive || !_cb.isExploring()) return;

        vec2 pc = _cb.getPlayerCenter();
        float dx = pc.x - _data->cx(), dy = pc.y - _data->cy();
        float dist = std::sqrt(dx*dx + dy*dy);

        if (dist < _data->aggroR) {
            _data->aggro = true;
            float nx = dx/dist, ny = dy/dist;
            float newX = _data->x + nx * 30.0f * dt;
            float newY = _data->y + ny * 30.0f * dt;

            if (!_cb.isSolid(newX, _data->y)              && !_cb.isSolid(newX + _data->w-1, _data->y) &&
                !_cb.isSolid(newX, _data->y + _data->h-1) && !_cb.isSolid(newX + _data->w-1, _data->y + _data->h-1))
                _data->x = newX;
            if (!_cb.isSolid(_data->x, newY)              && !_cb.isSolid(_data->x + _data->w-1, newY) &&
                !_cb.isSolid(_data->x, newY + _data->h-1) && !_cb.isSolid(_data->x + _data->w-1, _data->y + _data->h-1))
                _data->y = newY;

            _anim->play("walk");
        } else {
            _data->aggro = false;
            _anim->play("idle");
        }
        _anim->update(dt);

        if (AABBOverlaps(_cb.getPlayerAABB(), _data->aabb())) {
            _cb.onContact(_idx);
        }
    }

    void DrawImGui() override {
        if (ImGui::CollapsingHeader(fmt::format("EnemyAI [{}]", _data->def.name).c_str())) {
            ImGui::Text("Alive: %s   Aggro: %s",
                        _data->alive ? "yes" : "no", _data->aggro ? "yes" : "no");
            ImGui::Text("Pos: %.1f, %.1f", _data->x, _data->y);
            ImGui::DragFloat("Aggro radius", &_data->aggroR, 1.0f, 0.0f, 400.0f);
        }
    }
};

// Owns all turn-based battle state and drives the battle loop via OnTick.
// StartBattle transitions the game mode; EndBattle (private) returns to explore.
class BattleSystemComponent : public Component {
    Player*             _player;
    SpriteAnimator*     _playerAnim;
    std::vector<Enemy>* _enemies;
    GameMode*           _mode;
    float*              _transition;
    FontID              _fontID;
    GLuint              _playerTex, _enemyTex;
    int                 _screenW, _screenH;

    BattleState  _battle;
    float        _shakeMag   = 0;
    float        _shakeTimer = 0;
    std::mt19937 _rng{std::random_device{}()};
public:
    BattleSystemComponent(GameObject* go,
                          Player* player, SpriteAnimator* playerAnim,
                          std::vector<Enemy>* enemies,
                          GameMode* mode, float* transition,
                          FontID fontID, GLuint playerTex, GLuint enemyTex,
                          int screenW, int screenH)
        : _player(player), _playerAnim(playerAnim), _enemies(enemies),
          _mode(mode), _transition(transition),
          _fontID(fontID), _playerTex(playerTex), _enemyTex(enemyTex),
          _screenW(screenW), _screenH(screenH)
    {
        gameObject = go;
        _battle.playerStats = &_player->stats;
    }

    std::string GetName() const override { return "BattleSystemComponent"; }

    BattleState& GetBattle() { return _battle; }

    void StartBattle(int enemyIdx) {
        *_mode = GameMode::BattleTransitionIn;
        *_transition = 0.0f;

        Enemy& e = (*_enemies)[enemyIdx];
        e.resetBattleStats();

        _battle.phase = BattlePhase::PlayerMenu;
        _battle.menuSel = 0; _battle.skillSel = 0; _battle.itemSel = 0;
        _battle.targetIdx = 0; _battle.actionTimer = 0.0f;
        _battle.log.clear(); _battle.expGained = 0; _battle.goldGained = 0;

        _battle.enemyStats.clear();
        _battle.enemyNames.clear();
        _battle.enemyIndices.clear();
        _battle.enemyStats.push_back(e.battle);
        _battle.enemyNames.push_back(e.def.name);
        _battle.enemyIndices.push_back(enemyIdx);

        float dx = e.cx()-_player->cx(), dy = e.cy()-_player->cy();
        float len = std::sqrt(dx*dx+dy*dy);
        if (len > 0) { e.x += dx/len*32; e.y += dy/len*32; }

        _battle.pushLog(fmt::format("A {} appeared!", e.def.name));
        _battle.playerFirst = (_player->stats.spd >= e.def.stats.spd);
    }

    void OnTick(float dt) override {
        if (*_mode != GameMode::Battle) return;
        UpdateBattle(dt);
        DrawBattle();
    }

    void DrawImGui() override {
        if (!ImGui::CollapsingHeader("BattleSystemComponent")) return;
        ImGui::Text("Phase: %d", (int)_battle.phase);
        ImGui::Text("Level: %d  EXP: %d/%d  Gold: %d",
                    _battle.level, _battle.exp, _battle.expToNext, _battle.gold);
        ImGui::Text("Shake: %.2f (%.2fs)", _shakeMag, _shakeTimer);
    }

private:
    void EndBattle(bool victory) {
        if (victory) {
            _battle.exp  += _battle.expGained;
            _battle.gold += _battle.goldGained;
            while (_battle.exp >= _battle.expToNext) {
                _battle.exp -= _battle.expToNext;
                _battle.level++;
                _battle.expToNext = (int)(_battle.expToNext * 1.5f);
                _player->stats.maxHp += 8; _player->stats.maxMp += 4;
                _player->stats.atk   += 2; _player->stats.def   += 1;
                _player->stats.spd   += 1;
                _player->stats.hp = _player->stats.maxHp;
                _player->stats.mp = _player->stats.maxMp;
                _battle.pushLog(fmt::format("Level up! Now LV{}!", _battle.level));
            }
            for (int idx : _battle.enemyIndices)
                if (idx >= 0 && idx < (int)_enemies->size())
                    (*_enemies)[idx].alive = false;
        }
        *_mode = GameMode::BattleTransitionOut;
        *_transition = 1.0f;
    }

    int CalcDamage(int atk, int def) {
        std::uniform_int_distribution<int> jitter(-3,3);
        return std::max(1, atk - def/2 + jitter(_rng));
    }

    void ExecutePlayerAttack() {
        Stats& es = _battle.enemyStats[_battle.targetIdx];
        int dmg = CalcDamage(_player->stats.atk, es.def);
        es.takeDamage(dmg);
        _shakeMag = 4.0f; _shakeTimer = 0.3f;
        _battle.pushLog(fmt::format("You attack {} for {} dmg!",
            _battle.enemyNames[_battle.targetIdx], dmg));
        _battle.phase = BattlePhase::EnemyAction;
        _battle.actionTimer = 0.8f;
    }

    void ExecutePlayerSkill(int idx) {
        if (idx >= (int)_player->skills.size()) return;
        Skill& sk = _player->skills[idx];
        if (_player->stats.mp < sk.mpCost) {
            _battle.pushLog("Not enough MP!"); _battle.phase = BattlePhase::PlayerMenu; return;
        }
        _player->stats.mp -= sk.mpCost;
        if (sk.target == SkillTarget::AllEnemies) {
            int total = 0;
            for (auto& es : _battle.enemyStats) {
                if (es.isDead()) continue;
                int v = sk.calc(_player->stats, es); es.takeDamage(v); total += v;
            }
            _battle.pushLog(fmt::format("{} hits all for ~{} dmg!", sk.name,
                total/(int)_battle.enemyStats.size()));
        } else if (sk.target == SkillTarget::Self) {
            int v = sk.calc(_player->stats, _player->stats);
            _player->stats.heal(-v);
            _battle.pushLog(fmt::format("{}: restored {} HP!", sk.name, -v));
        } else {
            Stats& es = _battle.enemyStats[_battle.targetIdx];
            int v = sk.calc(_player->stats, es); es.takeDamage(v);
            _battle.pushLog(fmt::format("{} deals {} dmg!", sk.name, v));
        }
        _shakeMag = 6.0f; _shakeTimer = 0.3f;
        _battle.phase = BattlePhase::EnemyAction; _battle.actionTimer = 0.8f;
    }

    void ExecutePlayerItem(int idx) {
        if (idx >= (int)_player->items.size()) return;
        Item& it = _player->items[idx];
        if (it.count <= 0) { _battle.pushLog("None left!"); return; }
        it.count--;
        if (it.effect == ItemEffect::HealHP) {
            _player->stats.heal(it.amount);
            _battle.pushLog(fmt::format("{}: recovered {} HP!", it.name, it.amount));
        } else {
            _player->stats.restoreMp(it.amount);
            _battle.pushLog(fmt::format("{}: recovered {} MP!", it.name, it.amount));
        }
        _battle.phase = BattlePhase::EnemyAction; _battle.actionTimer = 0.8f;
    }

    void ExecuteEnemyTurn() {
        int actorIdx = -1;
        for (int i=0; i<(int)_battle.enemyStats.size(); i++)
            if (!_battle.enemyStats[i].isDead()) { actorIdx = i; break; }
        if (actorIdx < 0) return;
        const Enemy& worldE = (*_enemies)[_battle.enemyIndices[actorIdx]];
        int dmg = CalcDamage(worldE.def.stats.atk, _player->stats.def);
        _player->stats.takeDamage(dmg);
        _battle.pushLog(fmt::format("{} attacks for {} dmg!", worldE.def.name, dmg));
        _battle.phase = BattlePhase::PlayerMenu;
    }

    void UpdateBattle(float dt) {
        auto* inp = gameObject->GetApp()->GetInput();
        BattleState& b = _battle;
        if (_shakeTimer > 0) _shakeTimer -= dt; else _shakeMag = 0;
        b.tickLog(dt);
        switch (b.phase) {
        case BattlePhase::PlayerMenu: {
            int count = (int)BattleMenuSel::COUNT;
            if (inp->IsKeyPressed(Key::UP)   || inp->IsKeyPressed(Key::W)) b.menuSel=(b.menuSel+count-1)%count;
            if (inp->IsKeyPressed(Key::DOWN) || inp->IsKeyPressed(Key::S)) b.menuSel=(b.menuSel+1)%count;
            if (inp->IsKeyPressed(Key::Z)    || inp->IsKeyPressed(Key::ENTER)) {
                switch ((BattleMenuSel)b.menuSel) {
                case BattleMenuSel::Attack: ExecutePlayerAttack(); break;
                case BattleMenuSel::Skills: b.phase=BattlePhase::PlayerSkillMenu; b.skillSel=0; break;
                case BattleMenuSel::Items:  b.phase=BattlePhase::PlayerItemMenu;  b.itemSel=0;  break;
                case BattleMenuSel::Run:    b.phase=BattlePhase::Flee; b.actionTimer=0.5f; break;
                default: break;
                }
            }
            break;
        }
        case BattlePhase::PlayerSkillMenu: {
            int count = (int)_player->skills.size();
            if (inp->IsKeyPressed(Key::UP)   || inp->IsKeyPressed(Key::W)) b.skillSel=(b.skillSel+count-1)%count;
            if (inp->IsKeyPressed(Key::DOWN) || inp->IsKeyPressed(Key::S)) b.skillSel=(b.skillSel+1)%count;
            if (inp->IsKeyPressed(Key::Z)    || inp->IsKeyPressed(Key::ENTER)) ExecutePlayerSkill(b.skillSel);
            if (inp->IsKeyPressed(Key::X)    || inp->IsKeyPressed(Key::ESCAPE)) b.phase=BattlePhase::PlayerMenu;
            break;
        }
        case BattlePhase::PlayerItemMenu: {
            int count = (int)_player->items.size();
            if (inp->IsKeyPressed(Key::UP)   || inp->IsKeyPressed(Key::W)) b.itemSel=(b.itemSel+count-1)%count;
            if (inp->IsKeyPressed(Key::DOWN) || inp->IsKeyPressed(Key::S)) b.itemSel=(b.itemSel+1)%count;
            if (inp->IsKeyPressed(Key::Z)    || inp->IsKeyPressed(Key::ENTER)) ExecutePlayerItem(b.itemSel);
            if (inp->IsKeyPressed(Key::X)    || inp->IsKeyPressed(Key::ESCAPE)) b.phase=BattlePhase::PlayerMenu;
            break;
        }
        case BattlePhase::EnemyAction:
            b.actionTimer -= dt;
            if (b.actionTimer <= 0) {
                if (b.allEnemiesDead()) {
                    for (int idx : b.enemyIndices)
                        if (idx<(int)_enemies->size()) {
                            b.expGained  += (*_enemies)[idx].def.expReward;
                            b.goldGained += (*_enemies)[idx].def.goldReward;
                        }
                    b.pushLog(fmt::format("Victory! +{} EXP +{} Gold", b.expGained, b.goldGained));
                    b.phase = BattlePhase::Victory; b.actionTimer = 2.5f;
                } else if (_player->stats.isDead()) {
                    b.pushLog("You were defeated...");
                    b.phase = BattlePhase::Defeat; b.actionTimer = 2.5f;
                } else { ExecuteEnemyTurn(); }
            }
            break;
        case BattlePhase::Victory:
            b.actionTimer -= dt;
            if (b.actionTimer <= 0) EndBattle(true);
            break;
        case BattlePhase::Defeat:
            b.actionTimer -= dt;
            if (b.actionTimer <= 0) { _player->stats.hp = 1; EndBattle(false); }
            break;
        case BattlePhase::Flee:
            b.actionTimer -= dt;
            if (b.actionTimer <= 0) { b.pushLog("Escaped!"); EndBattle(false); }
            break;
        default: break;
        }
    }

    void DrawBattle() {
        auto* gfx = gameObject->GetApp()->GetGraphicsServer();
        const float W = (float)_screenW, H = (float)_screenH;
        gfx->DrawQuad(W*0.5f, H*0.5f, W, H, 0, glm::vec4(0.08f,0.06f,0.14f,1));
        for (int i=0; i<8; i++)
            gfx->DrawRect(0, i*H/8.0f, W, 1, glm::vec4(0.15f,0.12f,0.22f,0.5f));

        float shakeX = 0, shakeY = 0;
        if (_shakeTimer > 0) {
            std::uniform_real_distribution<float> d(-_shakeMag, _shakeMag);
            shakeX = d(_rng); shakeY = d(_rng);
        }

        constexpr int CCOLS=4, CROWS=2;
        for (int i=0; i<(int)_battle.enemyStats.size(); i++) {
            const Stats& es = _battle.enemyStats[i];
            if (es.isDead()) continue;
            float ex = W*0.65f+i*100+shakeX, ey = H*0.35f+shakeY, sz = 72;
            glm::vec2 uv0{0,0}, uv1{1.0f/CCOLS, 1.0f/CROWS};
            gfx->DrawSprite2D(ex-sz*0.5f, ey-sz*0.5f, sz, sz, _enemyTex, uv0, uv1,
                (_battle.targetIdx==i) ? glm::vec4(1,0.7f,0.7f,1) : glm::vec4(1));
            gfx->DrawText(_fontID, _battle.enemyNames[i], ex-30, ey-sz*0.5f-28, 0.6f, glm::vec4(1,0.8f,0.8f,1));
            DrawHPBar(gfx, ex-50, ey-sz*0.5f-14, 100, 8, es.hp, es.maxHp, glm::vec4(0.9f,0.2f,0.2f,1));
        }
        {
            auto [fc,fr] = _playerAnim->currentFrame();
            float px=W*0.25f, py=H*0.42f, sz=72;
            glm::vec2 uv0{(float)fc/CCOLS,(float)fr/CROWS};
            glm::vec2 uv1{(float)(fc+1)/CCOLS,(float)(fr+1)/CROWS};
            gfx->DrawSprite2D(px-sz*0.5f, py-sz*0.5f, sz, sz, _playerTex, uv0, uv1);
        }
        DrawBattleStats(gfx, W, H);
        DrawBattleMenu(gfx, W, H);
        DrawBattleLog(gfx, W, H);
        if (_battle.phase == BattlePhase::Victory) {
            DrawPanel(gfx, W*0.5f-120, H*0.5f-30, 240, 60, glm::vec4(0.05f,0.2f,0.05f,0.95f));
            gfx->DrawText(_fontID, "VICTORY!", W*0.5f-55, H*0.5f-14, 1.0f, glm::vec4(0.3f,1,0.4f,1));
        } else if (_battle.phase == BattlePhase::Defeat) {
            DrawPanel(gfx, W*0.5f-120, H*0.5f-30, 240, 60, glm::vec4(0.2f,0.03f,0.03f,0.95f));
            gfx->DrawText(_fontID, "DEFEATED", W*0.5f-55, H*0.5f-14, 1.0f, glm::vec4(1,0.3f,0.3f,1));
        }
    }

    void DrawBattleStats(GraphicsServer* gfx, float W, float H) {
        const Stats& s = _player->stats;
        DrawPanel(gfx, 8, H-110, 200, 100);
        gfx->DrawText(_fontID, fmt::format("LV{}", _battle.level),       16, H-106, 0.55f, glm::vec4(1,0.9f,0.5f,1));
        gfx->DrawText(_fontID, fmt::format("HP {}/{}", s.hp, s.maxHp),   16, H-90,  0.55f, glm::vec4(0.7f,1,0.7f,1));
        DrawHPBar(gfx, 16, H-78, 180, 10, s.hp, s.maxHp);
        gfx->DrawText(_fontID, fmt::format("MP {}/{}", s.mp, s.maxMp),   16, H-64,  0.55f, glm::vec4(0.5f,0.7f,1,1));
        DrawMPBar(gfx, 16, H-52, 180, 10, s.mp, s.maxMp);
        gfx->DrawText(_fontID, fmt::format("ATK:{} DEF:{}", s.atk, s.def), 16, H-36, 0.5f, glm::vec4(0.8f,0.8f,0.8f,0.9f));
    }

    void DrawBattleMenu(GraphicsServer* gfx, float W, float H) {
        const BattleState& b = _battle;
        if (b.phase == BattlePhase::PlayerMenu) {
            DrawPanel(gfx, W-180, H-130, 170, 120);
            const char* labels[] = {"Attack","Skills","Items","Run"};
            for (int i=0; i<4; i++) {
                bool sel = (b.menuSel==i);
                gfx->DrawText(_fontID, fmt::format("{} {}", sel?"▶":" ", labels[i]),
                    W-170, H-124+i*26, 0.65f, sel ? glm::vec4(1,1,0.3f,1) : glm::vec4(0.85f,0.85f,0.9f,1));
            }
            gfx->DrawText(_fontID, "Z:confirm", W-170, H-18, 0.45f, glm::vec4(0.5f,0.5f,0.6f,1));
        } else if (b.phase == BattlePhase::PlayerSkillMenu) {
            DrawPanel(gfx, W-220, H-200, 210, (int)_player->skills.size()*30+40);
            gfx->DrawText(_fontID, "─ Skills ─", W-210, H-194, 0.55f, glm::vec4(0.7f,0.7f,1,1));
            for (int i=0; i<(int)_player->skills.size(); i++) {
                bool sel = (b.skillSel==i);
                const Skill& sk = _player->skills[i];
                gfx->DrawText(_fontID, fmt::format("{}{} ({}MP)", sel?"▶":" ", sk.name, sk.mpCost),
                    W-210, H-178+i*28, 0.6f, sel ? glm::vec4(1,1,0.3f,1) : glm::vec4(0.85f,0.85f,0.9f,1));
            }
            gfx->DrawText(_fontID, "X:back", W-210, H-16, 0.45f, glm::vec4(0.5f,0.5f,0.6f,1));
        } else if (b.phase == BattlePhase::PlayerItemMenu) {
            DrawPanel(gfx, W-220, H-200, 210, (int)_player->items.size()*30+40);
            gfx->DrawText(_fontID, "─ Items ─", W-210, H-194, 0.55f, glm::vec4(0.7f,1,0.7f,1));
            for (int i=0; i<(int)_player->items.size(); i++) {
                bool sel = (b.itemSel==i);
                const Item& it = _player->items[i];
                gfx->DrawText(_fontID, fmt::format("{}{} x{}", sel?"▶":" ", it.name, it.count),
                    W-210, H-178+i*28, 0.6f,
                    sel ? glm::vec4(1,1,0.3f,1) : (it.count>0 ? glm::vec4(0.85f,0.85f,0.9f,1) : glm::vec4(0.4f,0.4f,0.4f,1)));
            }
            gfx->DrawText(_fontID, "X:back", W-210, H-16, 0.45f, glm::vec4(0.5f,0.5f,0.6f,1));
        }
    }

    void DrawBattleLog(GraphicsServer* gfx, float W, float H) {
        DrawPanel(gfx, 220, H-130, W-440, 120);
        int shown = std::min((int)_battle.log.size(), 4);
        for (int i=0; i<shown; i++) {
            const auto& entry = _battle.log[_battle.log.size()-shown+i];
            float alpha = std::min(1.0f, entry.ttl);
            gfx->DrawText(_fontID, entry.text, 232, H-124+i*26, 0.58f, glm::vec4(0.9f,0.9f,1,alpha));
        }
    }

    void DrawPanel(GraphicsServer* gfx, float x, float y, float w, float h,
                   glm::vec4 bg = glm::vec4(0.05f,0.05f,0.12f,0.92f)) {
        gfx->DrawQuad(x+w*0.5f, y+h*0.5f, w, h, 0, bg);
        gfx->DrawRect(x, y, w, h, glm::vec4(0.35f,0.35f,0.55f,0.8f));
    }
    void DrawHPBar(GraphicsServer* gfx, float x, float y, float w, float h,
                   int hp, int maxHp, glm::vec4 color = glm::vec4(0.2f,0.85f,0.2f,1)) {
        gfx->DrawQuad(x+w*0.5f, y+h*0.5f, w, h, 0, glm::vec4(0.1f,0.1f,0.1f,0.85f));
        float ratio = maxHp>0 ? (float)hp/maxHp : 0;
        float fw = (w-2)*ratio;
        if (fw > 0) gfx->DrawQuad(x+1+fw*0.5f, y+1+(h-2)*0.5f, fw, h-2, 0, color);
    }
    void DrawMPBar(GraphicsServer* gfx, float x, float y, float w, float h, int mp, int maxMp) {
        gfx->DrawQuad(x+w*0.5f, y+h*0.5f, w, h, 0, glm::vec4(0.1f,0.1f,0.1f,0.85f));
        float ratio = maxMp>0 ? (float)mp/maxMp : 0;
        float fw = (w-2)*ratio;
        if (fw > 0) gfx->DrawQuad(x+1+fw*0.5f, y+1+(h-2)*0.5f, fw, h-2, 0, glm::vec4(0.2f,0.4f,1,1));
    }
};

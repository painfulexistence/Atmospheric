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

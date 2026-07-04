#pragma once
// ============================================================================
//  CombatantComponent
//  ---------------------------------------------------------------------------
//  Lightweight "identity + layout" component shared by the player and every
//  enemy. It records the display name, which side the combatant is on, and the
//  on-screen rectangle used for both rendering and mouse hit-testing. It also
//  caches sibling components so the battle manager / renderer don't repeatedly
//  dynamic_cast.
// ============================================================================
#include "Atmospheric/component.hpp"
#include "Atmospheric/game_object.hpp"
#include "health_component.hpp"
#include "status_component.hpp"
#include <glm/vec2.hpp>
#include <string>

namespace CardGame {

    struct Rect {
        float x = 0, y = 0, w = 0, h = 0;
        bool Contains(glm::vec2 p) const {
            return p.x >= x && p.x <= x + w && p.y >= y && p.y <= y + h;
        }
        glm::vec2 Center() const {
            return { x + w * 0.5f, y + h * 0.5f };
        }
    };

    class CombatantComponent : public Component {
    public:
        CombatantComponent(GameObject*, std::string name, bool isPlayer) : _name(std::move(name)), _isPlayer(isPlayer) {
        }

        std::string GetName() const override {
            return "CombatantComponent";
        }
        bool CanTick() const override {
            return false;
        }

        void OnAttach() override {
            _health = gameObject->GetComponent<HealthComponent>();
            _status = gameObject->GetComponent<StatusComponent>();
        }

        const std::string& DisplayName() const {
            return _name;
        }
        bool IsPlayer() const {
            return _isPlayer;
        }

        HealthComponent* Health() const {
            return _health;
        }
        StatusComponent* Status() const {
            return _status;
        }
        bool IsAlive() const {
            return _health && !_health->IsDead();
        }

        Rect rect;// current on-screen box (set by the renderer)
        float hitFlash = 0.0f;// >0 right after taking damage (for a flash fx)

    private:
        std::string _name;
        bool _isPlayer;
        HealthComponent* _health = nullptr;
        StatusComponent* _status = nullptr;
    };

}// namespace CardGame

#pragma once
// ============================================================================
//  HealthComponent
//  ---------------------------------------------------------------------------
//  HP, max HP and the per-turn Block pool. Damage is absorbed by Block first;
//  Block is wiped at the owner's turn start (ResetBlock).
// ============================================================================
#include "Atmospheric/component.hpp"
#include <algorithm>

namespace CardGame {

    class HealthComponent : public Component {
    public:
        HealthComponent(GameObject*, int maxHp) : _hp(maxHp), _maxHp(maxHp) {
        }

        std::string GetName() const override {
            return "HealthComponent";
        }
        bool CanTick() const override {
            return false;
        }

        int Hp() const {
            return _hp;
        }
        int MaxHp() const {
            return _maxHp;
        }
        int Block() const {
            return _block;
        }
        bool IsDead() const {
            return _hp <= 0;
        }

        void AddBlock(int amount) {
            _block += std::max(0, amount);
        }
        void ResetBlock() {
            _block = 0;
        }

        void Heal(int amount) {
            _hp = std::min(_maxHp, _hp + std::max(0, amount));
        }

        // Block-absorbing damage. Returns the HP actually lost (after block).
        int TakeDamage(int dmg) {
            dmg = std::max(0, dmg);
            int absorbed = std::min(_block, dmg);
            _block -= absorbed;
            int through = dmg - absorbed;
            _hp = std::max(0, _hp - through);
            return through;
        }

        // Unblockable HP loss (poison, etc.).
        void LoseHp(int amount) {
            _hp = std::max(0, _hp - std::max(0, amount));
        }

    private:
        int _hp;
        int _maxHp;
        int _block = 0;
    };

}// namespace CardGame

#pragma once
// ============================================================================
//  StatusComponent
//  ---------------------------------------------------------------------------
//  Holds the buff/debuff stacks for a single combatant and answers the two
//  damage questions the battle system asks:
//    - how much damage does this combatant *deal*   (Strength, Weak)
//    - how much damage does this combatant *take*    (Vulnerable)
//
//  Turn bookkeeping (poison ticking, debuff decay) is also encapsulated here so
//  the battle manager just calls OnTurnStart()/OnTurnEnd().
// ============================================================================
#include "../game_data.hpp"
#include "Atmospheric/component.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace CardGame {

    enum class StatusType { Vulnerable, Weak, Poison, Strength, COUNT };

    inline const char* StatusName(StatusType t) {
        switch (t) {
        case StatusType::Vulnerable:
            return "Vuln";
        case StatusType::Weak:
            return "Weak";
        case StatusType::Poison:
            return "Psn";
        case StatusType::Strength:
            return "Str";
        default:
            return "?";
        }
    }

    class StatusComponent : public Component {
    public:
        explicit StatusComponent(GameObject*) {
        }

        std::string GetName() const override {
            return "StatusComponent";
        }
        bool CanTick() const override {
            return false;
        }

        int Get(StatusType t) const {
            return _stacks[static_cast<int>(t)];
        }
        bool Has(StatusType t) const {
            return _stacks[static_cast<int>(t)] > 0;
        }

        void Add(StatusType t, int amount) {
            _stacks[static_cast<int>(t)] = std::max(0, _stacks[static_cast<int>(t)] + amount);
        }
        void Clear(StatusType t) {
            _stacks[static_cast<int>(t)] = 0;
        }
        void ClearAll() {
            _stacks.fill(0);
        }

        // Damage this combatant deals: + Strength, then x Weak.
        int ModifyOutgoingDamage(int base) const {
            int dmg = base + Get(StatusType::Strength);
            if (Has(StatusType::Weak)) dmg = static_cast<int>(std::floor(dmg * Tuning::WEAK_MULT));
            return std::max(0, dmg);
        }

        // Damage this combatant takes: x Vulnerable.
        int ModifyIncomingDamage(int dmg) const {
            if (Has(StatusType::Vulnerable)) dmg = static_cast<int>(std::floor(dmg * Tuning::VULNERABLE_MULT));
            return std::max(0, dmg);
        }

        // Called at the start of this combatant's turn.
        // Returns the poison damage that should be applied to its health.
        int OnTurnStart() {
            int poison = Get(StatusType::Poison);
            if (poison > 0) Add(StatusType::Poison, -1);// poison wanes by 1 each turn
            return poison;
        }

        // Called at the end of this combatant's turn: timed debuffs decay.
        void OnTurnEnd() {
            decay(StatusType::Vulnerable);
            decay(StatusType::Weak);
        }

    private:
        void decay(StatusType t) {
            if (_stacks[static_cast<int>(t)] > 0) _stacks[static_cast<int>(t)]--;
        }

        std::array<int, static_cast<int>(StatusType::COUNT)> _stacks{};
    };

}// namespace CardGame

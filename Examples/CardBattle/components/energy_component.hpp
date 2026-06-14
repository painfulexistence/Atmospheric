#pragma once
// ============================================================================
//  EnergyComponent
//  ---------------------------------------------------------------------------
//  The player's per-turn energy pool used to pay card costs.
// ============================================================================
#include "Atmospheric/component.hpp"
#include "../game_data.hpp"

namespace CardGame {

class EnergyComponent : public Component {
public:
    explicit EnergyComponent(GameObject*) {}

    std::string GetName() const override { return "EnergyComponent"; }
    bool CanTick() const override { return false; }

    int Energy()    const { return _energy; }
    int MaxEnergy() const { return _max; }

    void Reset()       { _energy = _max; }
    void Gain(int n)   { _energy += n; }
    bool CanAfford(int cost) const { return _energy >= cost; }
    bool Spend(int cost) {
        if (_energy < cost) return false;
        _energy -= cost;
        return true;
    }

private:
    int _max    = Tuning::PLAYER_ENERGY;
    int _energy = Tuning::PLAYER_ENERGY;
};

} // namespace CardGame

#pragma once
// ============================================================================
//  EnemyBrainComponent
//  ---------------------------------------------------------------------------
//  Drives an enemy's AI. It owns a pointer to the enemy's data definition,
//  tracks which move is *telegraphed* for next turn (so the player can plan),
//  and picks the following move. The AI is intentionally simple and data-driven:
//  pick a random move, but avoid using the same move three times in a row.
// ============================================================================
#include "Atmospheric/component.hpp"
#include "../game_data.hpp"
#include <random>

namespace CardGame {

class EnemyBrainComponent : public Component {
public:
    EnemyBrainComponent(GameObject*, const EnemyDef* def, unsigned seed)
        : _def(def), _rng(seed) {
        ChooseNextMove();
    }

    std::string GetName() const override { return "EnemyBrainComponent"; }
    bool CanTick() const override { return false; }

    const EnemyDef&  Def()          const { return *_def; }
    const EnemyMove& TelegraphedMove() const { return _def->moves[_moveIdx]; }

    void ChooseNextMove() {
        const int n = (int)_def->moves.size();
        if (n <= 1) { _moveIdx = 0; return; }

        int next = _moveIdx;
        // Re-roll while we'd be repeating the same move a 3rd time.
        for (int tries = 0; tries < 8; ++tries) {
            next = (int)(_rng() % n);
            if (!(next == _lastMove && _repeats >= 2)) break;
        }
        _repeats  = (next == _lastMove) ? _repeats + 1 : 0;
        _lastMove = next;
        _moveIdx  = next;
    }

private:
    const EnemyDef* _def;
    std::mt19937    _rng;
    int             _moveIdx  = 0;
    int             _lastMove = -1;
    int             _repeats  = 0;
};

} // namespace CardGame

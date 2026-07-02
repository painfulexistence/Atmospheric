#pragma once
// ============================================================================
//  DeckComponent
//  ---------------------------------------------------------------------------
//  The player's card piles: draw / hand / discard / exhaust. Stores card ids
//  (indices into CardDB). When the draw pile runs dry it reshuffles the
//  discard pile back in, Slay-the-Spire style.
// ============================================================================
#include "Atmospheric/component.hpp"
#include "../game_data.hpp"
#include <algorithm>
#include <random>

namespace CardGame {

class DeckComponent : public Component {
public:
    DeckComponent(GameObject*, std::vector<int> startingDeck, unsigned seed)
        : _master(std::move(startingDeck)), _rng(seed) {}

    std::string GetName() const override { return "DeckComponent"; }
    bool CanTick() const override { return false; }

    const std::vector<int>& Hand()    const { return _hand; }
    const std::vector<int>& Master()  const { return _master; }
    int DrawCount()    const { return static_cast<int>(_draw.size()); }
    int DiscardCount() const { return static_cast<int>(_discard.size()); }

    // Permanently add a card to the deck (post-battle reward).
    void AddCard(int cardId) { _master.push_back(cardId); }

    // Rebuild all piles from the master list for a fresh fight.
    void ResetForBattle() {
        _draw = _master;
        _hand.clear();
        _discard.clear();
        _exhaust.clear();
        shuffle(_draw);
    }

    void Draw(int n) { for (int i = 0; i < n; ++i) drawOne(); }

    void DiscardHand() {
        _discard.insert(_discard.end(), _hand.begin(), _hand.end());
        _hand.clear();
    }

    // Remove the card at hand index, sending it to discard or exhaust.
    void RemoveFromHand(int handIdx, bool exhaust) {
        if (handIdx < 0 || handIdx >= static_cast<int>(_hand.size())) return;
        int id = _hand[handIdx];
        _hand.erase(_hand.begin() + handIdx);
        if (exhaust) _exhaust.push_back(id);
        else         _discard.push_back(id);
    }

private:
    void drawOne() {
        if (static_cast<int>(_hand.size()) >= Tuning::MAX_HAND_SIZE) return;
        if (_draw.empty()) reshuffleDiscardIntoDraw();
        if (_draw.empty()) return;
        _hand.push_back(_draw.back());
        _draw.pop_back();
    }

    void reshuffleDiscardIntoDraw() {
        _draw.insert(_draw.end(), _discard.begin(), _discard.end());
        _discard.clear();
        shuffle(_draw);
    }

    void shuffle(std::vector<int>& pile) {
        std::shuffle(pile.begin(), pile.end(), _rng);
    }

    std::vector<int> _master;   // the persistent deck
    std::vector<int> _draw, _hand, _discard, _exhaust;
    std::mt19937     _rng;
};

} // namespace CardGame

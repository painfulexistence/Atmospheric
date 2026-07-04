#pragma once
// ============================================================================
//  BattleManagerComponent
//  ---------------------------------------------------------------------------
//  The battle "system". It is itself an engine Component (attached to a manager
//  GameObject) and owns the turn-based state machine that ties all the other
//  components together:
//
//    - spawns the player + enemy GameObjects (each a bag of components)
//    - resolves card / enemy effects against HealthComponent + StatusComponent
//    - sequences player turn -> enemy turn -> reward -> next encounter
//
//  It contains NO tuning numbers and NO content; everything it acts on comes
//  from game_data.hpp. Rendering and raw input live in main.cpp — this class
//  only exposes queries (getters) and intents (TryPlayCard / EndTurn / ...).
// ============================================================================
#include "Atmospheric/application.hpp"
#include "Atmospheric/game_object.hpp"

#include "game_data.hpp"
#include "components/health_component.hpp"
#include "components/status_component.hpp"
#include "components/energy_component.hpp"
#include "components/deck_component.hpp"
#include "components/enemy_brain_component.hpp"
#include "components/combatant_component.hpp"

#include <random>
#include <string>
#include <vector>

namespace CardGame {

enum class Phase {
    PlayerTurn,   // player is free to play cards
    EnemyTurn,    // enemies act one at a time (with delays)
    Reward,       // pick a card after clearing a fight
    Won,          // all encounters cleared
    Defeat,       // player died
};

class BattleManagerComponent : public Component {
public:
    explicit BattleManagerComponent(GameObject*, Application* app)
        : _app(app), _rng(std::random_device{}()) {}

    std::string GetName() const override { return "BattleManagerComponent"; }
    bool CanTick() const override { return true; }

    // ---- queries used by the renderer --------------------------------------
    Phase phase() const { return _phase; }
    CombatantComponent* player() const { return _player; }
    const std::vector<CombatantComponent*>& enemies() const { return _enemies; }
    DeckComponent*   deck()   const { return _deck; }
    EnergyComponent* energy() const { return _energy; }
    const std::vector<std::string>& log() const { return _log; }
    int  selectedTarget() const { return _target; }
    int  encounterIndex() const { return _encounter; }
    int  encounterCount() const { return static_cast<int>(Encounters().size()); }
    const std::vector<int>& rewardCards() const { return _reward; }
    float bannerTimer() const { return _bannerTimer; }

    // The damage number to telegraph above an enemy (folds in Strength/Weak on
    // the enemy and Vulnerable on the player). 0 if the move is not an attack.
    int telegraphDamage(CombatantComponent* enemy) const {
        auto* brain = enemy->gameObject->GetComponent<EnemyBrainComponent>();
        if (!brain) return 0;
        const auto& mv = brain->TelegraphedMove();
        int base = BaseDamageOf(mv.effects);
        if (base <= 0) return 0;
        int dmg = enemy->Status()->ModifyOutgoingDamage(base);
        dmg = _player->Status()->ModifyIncomingDamage(dmg);
        return dmg;
    }
    IntentKind telegraphIntent(CombatantComponent* enemy) const {
        auto* brain = enemy->gameObject->GetComponent<EnemyBrainComponent>();
        return brain ? brain->TelegraphedMove().intent : IntentKind::Attack;
    }

    // ---- intents driven by input -------------------------------------------
    void SetTarget(int idx) {
        if (idx >= 0 && idx < static_cast<int>(_enemies.size()) && _enemies[idx]->IsAlive())
            _target = idx;
    }
    void CycleTarget() {
        for (int step = 1; step <= static_cast<int>(_enemies.size()); ++step) {
            int i = (_target + step) % static_cast<int>(_enemies.size());
            if (_enemies[i]->IsAlive()) { _target = i; break; }
        }
    }

    // Returns true if the card was actually played.
    bool TryPlayCard(int handIdx, int targetIdx) {
        if (_phase != Phase::PlayerTurn) return false;
        if (handIdx < 0 || handIdx >= static_cast<int>(_deck->Hand().size())) return false;

        const CardDef& card = GetCard(_deck->Hand()[handIdx]);
        if (!_energy->CanAfford(card.cost)) {
            pushLog("Not enough energy for " + card.name);
            return false;
        }

        std::vector<CombatantComponent*> targets;
        switch (card.target) {
            case TargetKind::Self:
                targets.push_back(_player);
                break;
            case TargetKind::Enemy: {
                CombatantComponent* t = pickEnemy(targetIdx);
                if (!t) return false;
                targets.push_back(t);
                break;
            }
            case TargetKind::AllEnemies:
                for (auto* e : _enemies) if (e->IsAlive()) targets.push_back(e);
                if (targets.empty()) return false;
                break;
        }

        _energy->Spend(card.cost);
        applyEffects(card.effects, _player, targets);
        _deck->RemoveFromHand(handIdx, card.exhaust);
        pushLog("You play " + card.name);

        checkBattleState();
        return true;
    }

    void EndTurn() {
        if (_phase != Phase::PlayerTurn) return;
        _player->Status()->OnTurnEnd();
        _deck->DiscardHand();
        _phase       = Phase::EnemyTurn;
        _actIdx      = 0;
        _bannerTimer = Tuning::TURN_BANNER_TIME;
        pushLog("-- Enemy turn --");
    }

    // After Reward (or to skip it). choice < 0 skips taking a card.
    void TakeReward(int choice) {
        if (_phase != Phase::Reward) return;
        if (choice >= 0 && choice < static_cast<int>(_reward.size())) {
            _deck->AddCard(_reward[choice]);
            pushLog("Added " + GetCard(_reward[choice]).name + " to deck");
        }
        _encounter++;
        startEncounter(_encounter);
        startPlayerTurn();
    }

    // (Re)start the whole run from scratch.
    void StartRun() {
        buildPlayer();
        _encounter = 0;
        _log.clear();
        startEncounter(0);
        startPlayerTurn();
    }

    // ---- per-frame: drives the enemy turn pacing ---------------------------
    void OnTick(float dt) override {
        if (_player) _player->hitFlash = std::max(0.0f, _player->hitFlash - dt * 3.0f);
        for (auto* e : _enemies) e->hitFlash = std::max(0.0f, e->hitFlash - dt * 3.0f);

        if (_phase != Phase::EnemyTurn) return;

        _bannerTimer -= dt;
        if (_bannerTimer > 0.0f) return;

        _actTimer -= dt;
        if (_actTimer > 0.0f) return;

        stepEnemyTurn();
    }

private:
    // ---- spawning ----------------------------------------------------------
    void buildPlayer() {
        if (_playerGO) _playerGO->SetActive(false);

        _playerGO = _app->CreateGameObject(glm::vec2(0.0f));
        _playerGO->SetName("Player");
        _playerGO->AddComponent<HealthComponent>(Tuning::PLAYER_MAX_HP);
        _playerGO->AddComponent<StatusComponent>();
        _playerGO->AddComponent<EnergyComponent>();
        _playerGO->AddComponent<DeckComponent>(StartingDeck(), static_cast<unsigned>(_rng()));
        _playerGO->AddComponent<CombatantComponent>(std::string("Hero"), true);

        _player = _playerGO->GetComponent<CombatantComponent>();
        _deck   = _playerGO->GetComponent<DeckComponent>();
        _energy = _playerGO->GetComponent<EnergyComponent>();
    }

    void startEncounter(int idx) {
        for (auto* go : _enemyGOs) go->SetActive(false);
        _enemyGOs.clear();
        _enemies.clear();

        const auto& layout = Encounters()[idx];
        for (int enemyId : layout) {
            const EnemyDef& def = EnemyDB()[enemyId];
            GameObject* go = _app->CreateGameObject(glm::vec2(0.0f));
            go->SetName(def.name);
            go->AddComponent<HealthComponent>(def.maxHp);
            go->AddComponent<StatusComponent>();
            go->AddComponent<EnemyBrainComponent>(&def, static_cast<unsigned>(_rng()));
            go->AddComponent<CombatantComponent>(def.name, false);
            _enemyGOs.push_back(go);
            _enemies.push_back(go->GetComponent<CombatantComponent>());
        }

        // Fresh combat: clear lingering player state, rebuild the deck.
        _player->Status()->ClearAll();
        _player->Health()->ResetBlock();
        _deck->ResetForBattle();
        _target = 0;
        pushLog("Encounter " + std::to_string(idx + 1) + " / " +
                std::to_string(encounterCount()));
    }

    void startPlayerTurn() {
        _phase = Phase::PlayerTurn;
        _player->Health()->ResetBlock();
        int poison = _player->Status()->OnTurnStart();
        if (poison > 0) {
            _player->Health()->LoseHp(poison);
            _player->hitFlash = 1.0f;
            pushLog("You suffer " + std::to_string(poison) + " poison");
        }
        _energy->Reset();
        _deck->Draw(Tuning::DRAW_PER_TURN);
        ensureValidTarget();
        checkBattleState();
    }

    // ---- enemy turn --------------------------------------------------------
    void stepEnemyTurn() {
        // Find the next enemy that still needs to act.
        while (_actIdx < static_cast<int>(_enemies.size()) && !_enemies[_actIdx]->IsAlive())
            _actIdx++;

        if (_actIdx >= static_cast<int>(_enemies.size())) {
            startPlayerTurn();   // all enemies have acted
            return;
        }

        CombatantComponent* e = _enemies[_actIdx];
        auto* brain = e->gameObject->GetComponent<EnemyBrainComponent>();

        e->Health()->ResetBlock();
        int poison = e->Status()->OnTurnStart();
        if (poison > 0) {
            e->Health()->LoseHp(poison);
            e->hitFlash = 1.0f;
            pushLog(e->DisplayName() + " takes " + std::to_string(poison) + " poison");
        }

        if (e->IsAlive()) {
            const EnemyMove& mv = brain->TelegraphedMove();
            std::vector<CombatantComponent*> targets{ _player };
            applyEffects(mv.effects, e, targets);
            pushLog(e->DisplayName() + " uses " + mv.name);
            e->Status()->OnTurnEnd();
            brain->ChooseNextMove();
        }

        _actIdx++;
        _actTimer = Tuning::ENEMY_ACT_DELAY;

        if (checkBattleState()) return; // player may have died
    }

    // ---- effect resolution -------------------------------------------------
    void applyEffects(const std::vector<Effect>& effects,
                      CombatantComponent* caster,
                      const std::vector<CombatantComponent*>& targets) {
        for (const Effect& e : effects) {
            if (IsTargetEffect(e.kind)) {
                for (auto* t : targets) applyTargetEffect(e, caster, t);
            } else {
                applyCasterEffect(e, caster);
            }
        }
    }

    void applyTargetEffect(const Effect& e, CombatantComponent* caster,
                           CombatantComponent* target) {
        switch (e.kind) {
            case EffectKind::Damage: {
                int dmg = caster->Status()->ModifyOutgoingDamage(e.value);
                dmg     = target->Status()->ModifyIncomingDamage(dmg);
                int lost = target->Health()->TakeDamage(dmg);
                if (lost > 0) target->hitFlash = 1.0f;
                break;
            }
            case EffectKind::ApplyVulnerable:
                target->Status()->Add(StatusType::Vulnerable, e.value); break;
            case EffectKind::ApplyWeak:
                target->Status()->Add(StatusType::Weak, e.value); break;
            case EffectKind::ApplyPoison:
                target->Status()->Add(StatusType::Poison, e.value); break;
            default: break;
        }
    }

    void applyCasterEffect(const Effect& e, CombatantComponent* caster) {
        switch (e.kind) {
            case EffectKind::Block:
                caster->Health()->AddBlock(e.value); break;
            case EffectKind::Heal:
                caster->Health()->Heal(e.value); break;
            case EffectKind::GainStrength:
                caster->Status()->Add(StatusType::Strength, e.value); break;
            case EffectKind::DrawCards:
                if (caster->IsPlayer() && _deck) _deck->Draw(e.value);
                break;
            case EffectKind::GainEnergy:
                if (caster->IsPlayer() && _energy) _energy->Gain(e.value);
                break;
            default: break;
        }
    }

    // ---- state transitions -------------------------------------------------
    // Returns true if the battle (or run) ended this call.
    bool checkBattleState() {
        if (_player->Health()->IsDead()) {
            _phase = Phase::Defeat;
            pushLog("You have been defeated...");
            return true;
        }
        bool allDead = true;
        for (auto* e : _enemies) if (e->IsAlive()) { allDead = false; break; }
        if (allDead) {
            if (_encounter + 1 >= encounterCount()) {
                _phase = Phase::Won;
                pushLog("You cleared every encounter! Victory!");
            } else {
                _player->Health()->Heal(Tuning::REWARD_HEAL);
                generateReward();
                _phase = Phase::Reward;
                pushLog("Victory! Heal " + std::to_string(Tuning::REWARD_HEAL) + " HP");
            }
            return true;
        }
        return false;
    }

    void generateReward() {
        _reward.clear();
        std::vector<int> pool = RewardPool();
        std::shuffle(pool.begin(), pool.end(), _rng);
        int n = std::min(Tuning::REWARD_CHOICES, static_cast<int>(pool.size()));
        for (int i = 0; i < n; ++i) _reward.push_back(pool[i]);
    }

    CombatantComponent* pickEnemy(int idx) {
        if (idx >= 0 && idx < static_cast<int>(_enemies.size()) && _enemies[idx]->IsAlive())
            return _enemies[idx];
        for (auto* e : _enemies) if (e->IsAlive()) return e;
        return nullptr;
    }

    void ensureValidTarget() {
        if (_target < static_cast<int>(_enemies.size()) && _enemies[_target]->IsAlive()) return;
        for (int i = 0; i < static_cast<int>(_enemies.size()); ++i)
            if (_enemies[i]->IsAlive()) { _target = i; return; }
    }

    void pushLog(std::string msg) {
        _log.push_back(std::move(msg));
        if (_log.size() > 6) _log.erase(_log.begin());
    }

    // ---- data --------------------------------------------------------------
    Application* _app = nullptr;
    std::mt19937 _rng;

    GameObject*      _playerGO = nullptr;
    CombatantComponent* _player = nullptr;
    DeckComponent*   _deck   = nullptr;
    EnergyComponent* _energy = nullptr;

    std::vector<GameObject*>          _enemyGOs;
    std::vector<CombatantComponent*>  _enemies;

    Phase _phase      = Phase::PlayerTurn;
    int   _encounter  = 0;
    int   _target     = 0;

    // enemy-turn pacing
    int   _actIdx     = 0;
    float _actTimer   = 0.0f;
    float _bannerTimer = 0.0f;

    std::vector<int>         _reward;
    std::vector<std::string> _log;
};

} // namespace CardGame

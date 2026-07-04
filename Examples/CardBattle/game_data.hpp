#pragma once
// ============================================================================
//  game_data.hpp
//  ---------------------------------------------------------------------------
//  ALL tunable numbers and content for the card battler live here.
//
//  Everything in this file is *pure data* (no logic): designers/balancers can
//  freely edit HP values, damage numbers, costs, the contents of the starting
//  deck, enemy move sets and encounter layouts without ever touching the
//  gameplay code in components/ or battle_manager.hpp.
//
//  Card and enemy effects are expressed as lists of {EffectKind, value} pairs,
//  so a new card is just another data row — no new code required.
// ============================================================================
#include <string>
#include <vector>

namespace CardGame {

    // ----------------------------------------------------------------------------
    //  Global tuning knobs
    // ----------------------------------------------------------------------------
    namespace Tuning {
        inline constexpr int PLAYER_MAX_HP = 80;// starting / max player HP
        inline constexpr int PLAYER_ENERGY = 3;// energy regained each turn
        inline constexpr int DRAW_PER_TURN = 5;// cards drawn at turn start
        inline constexpr int MAX_HAND_SIZE = 10;// hand size cap

        inline constexpr float VULNERABLE_MULT = 1.50f;// dmg taken while Vulnerable
        inline constexpr float WEAK_MULT = 0.75f;// dmg dealt while Weak

        inline constexpr int REWARD_HEAL = 25;// HP healed after each win
        inline constexpr int REWARD_CHOICES = 3;// cards offered after a win

        inline constexpr float ENEMY_ACT_DELAY = 0.7f;// seconds between enemy moves
        inline constexpr float TURN_BANNER_TIME = 0.9f;// "Enemy Turn" banner duration
    }// namespace Tuning

    // ----------------------------------------------------------------------------
    //  Effects — the atomic, data-only building block shared by cards & enemy moves
    // ----------------------------------------------------------------------------
    enum class EffectKind {
        // --- applied to the TARGET ---
        Damage,// value = base damage
        ApplyVulnerable,// value = stacks (target takes +50% dmg)
        ApplyWeak,// value = stacks (target deals -25% dmg)
        ApplyPoison,// value = stacks (target loses HP each turn)
        // --- applied to the CASTER ---
        Block,// value = block gained
        Heal,// value = HP healed
        GainStrength,// value = permanent +damage on attacks
        DrawCards,// value = cards to draw (player only)
        GainEnergy,// value = energy gained (player only)
    };

    struct Effect {
        EffectKind kind;
        int value = 0;
    };

    // True if the effect lands on the chosen target rather than the caster.
    inline bool IsTargetEffect(EffectKind k) {
        return k == EffectKind::Damage || k == EffectKind::ApplyVulnerable || k == EffectKind::ApplyWeak
               || k == EffectKind::ApplyPoison;
    }

    // Convenience: the base damage value of an effect list (0 if none).
    inline int BaseDamageOf(const std::vector<Effect>& effects) {
        for (const auto& e : effects)
            if (e.kind == EffectKind::Damage) return e.value;
        return 0;
    }

    // ----------------------------------------------------------------------------
    //  Cards
    // ----------------------------------------------------------------------------
    enum class CardKind { Attack, Skill, Power };
    enum class TargetKind { Enemy, Self, AllEnemies };

    struct CardDef {
        int id;
        std::string name;
        int cost;
        CardKind kind;
        TargetKind target;
        std::vector<Effect> effects;
        std::string text;// rules text shown on the card
        bool exhaust = false;
    };

    // The master card list. `id` matches the index for O(1) lookup.
    inline const std::vector<CardDef>& CardDB() {
        static const std::vector<CardDef> db = {
            { 0, "Strike", 1, CardKind::Attack, TargetKind::Enemy, { { EffectKind::Damage, 6 } }, "Deal 6 damage." },
            { 1, "Defend", 1, CardKind::Skill, TargetKind::Self, { { EffectKind::Block, 5 } }, "Gain 5 Block." },
            { 2,
              "Bash",
              2,
              CardKind::Attack,
              TargetKind::Enemy,
              { { EffectKind::Damage, 8 }, { EffectKind::ApplyVulnerable, 2 } },
              "Deal 8. Apply 2 Vulnerable." },
            { 3,
              "Cleave",
              1,
              CardKind::Attack,
              TargetKind::AllEnemies,
              { { EffectKind::Damage, 8 } },
              "Deal 8 to ALL enemies." },
            { 4,
              "Pommel Strike",
              1,
              CardKind::Attack,
              TargetKind::Enemy,
              { { EffectKind::Damage, 9 }, { EffectKind::DrawCards, 1 } },
              "Deal 9. Draw 1 card." },
            { 5,
              "Iron Wave",
              1,
              CardKind::Attack,
              TargetKind::Enemy,
              { { EffectKind::Damage, 5 }, { EffectKind::Block, 5 } },
              "Deal 5. Gain 5 Block." },
            { 6,
              "Inflame",
              1,
              CardKind::Power,
              TargetKind::Self,
              { { EffectKind::GainStrength, 2 } },
              "Gain 2 Strength." },
            { 7,
              "Poison Stab",
              1,
              CardKind::Attack,
              TargetKind::Enemy,
              { { EffectKind::Damage, 3 }, { EffectKind::ApplyPoison, 3 } },
              "Deal 3. Apply 3 Poison." },
            { 8,
              "Shrug It Off",
              1,
              CardKind::Skill,
              TargetKind::Self,
              { { EffectKind::Block, 8 }, { EffectKind::DrawCards, 1 } },
              "Gain 8 Block. Draw 1 card." },
            { 9,
              "Second Wind",
              1,
              CardKind::Skill,
              TargetKind::Self,
              { { EffectKind::Heal, 6 }, { EffectKind::Block, 4 } },
              "Heal 6 HP. Gain 4 Block." },
            { 10,
              "Whirlwind",
              2,
              CardKind::Attack,
              TargetKind::AllEnemies,
              { { EffectKind::Damage, 5 }, { EffectKind::Block, 6 } },
              "Deal 5 to ALL. Gain 6 Block." },
            { 11,
              "Heavy Blow",
              2,
              CardKind::Attack,
              TargetKind::Enemy,
              { { EffectKind::Damage, 14 } },
              "Deal 14 damage." },
        };
        return db;
    }

    inline const CardDef& GetCard(int id) {
        return CardDB()[id];
    }

    // The deck the player starts a run with (list of card ids).
    inline std::vector<int> StartingDeck() {
        return { 0, 0, 0, 0, 0,// 5x Strike
                 1, 1, 1, 1,// 4x Defend
                 2 };// 1x Bash
    }

    // Cards that can show up as post-battle rewards.
    inline std::vector<int> RewardPool() {
        return { 3, 4, 5, 6, 7, 8, 9, 10, 11 };
    }

    // ----------------------------------------------------------------------------
    //  Enemies
    // ----------------------------------------------------------------------------
    enum class IntentKind { Attack, Defend, Buff, Debuff };

    struct EnemyMove {
        std::string name;
        IntentKind intent;
        std::vector<Effect> effects;// target effects hit the player; caster effects hit self
    };

    struct EnemyDef {
        std::string name;
        int maxHp;
        std::vector<EnemyMove> moves;// AI picks among these (see EnemyBrainComponent)
    };

    inline const std::vector<EnemyDef>& EnemyDB() {
        static const std::vector<EnemyDef> db = {
            // 0 — Jaw Worm: aggressive bruiser
            { "Jaw Worm",
              44,
              {
                  { "Chomp", IntentKind::Attack, { { EffectKind::Damage, 10 } } },
                  { "Bellow", IntentKind::Buff, { { EffectKind::GainStrength, 3 }, { EffectKind::Block, 6 } } },
                  { "Thrash", IntentKind::Attack, { { EffectKind::Damage, 7 }, { EffectKind::Block, 5 } } },
              } },
            // 1 — Cultist: ramps up over time
            { "Cultist",
              48,
              {
                  { "Incantation", IntentKind::Buff, { { EffectKind::GainStrength, 2 } } },
                  { "Dark Strike", IntentKind::Attack, { { EffectKind::Damage, 6 } } },
              } },
            // 2 — Acid Slime: weakens the player
            { "Acid Slime",
              32,
              {
                  { "Corrosive Spit", IntentKind::Attack, { { EffectKind::Damage, 7 }, { EffectKind::ApplyWeak, 1 } } },
                  { "Lick", IntentKind::Debuff, { { EffectKind::ApplyWeak, 1 } } },
                  { "Tackle", IntentKind::Attack, { { EffectKind::Damage, 8 } } },
              } },
            // 3 — Spike Slime: tanky poison-punisher
            { "Spike Slime",
              40,
              {
                  { "Flame Tackle", IntentKind::Attack, { { EffectKind::Damage, 8 } } },
                  { "Harden", IntentKind::Defend, { { EffectKind::Block, 10 } } },
              } },
        };
        return db;
    }

    // ----------------------------------------------------------------------------
    //  Encounters — each entry is one fight, listed as the enemy ids it contains.
    //  Fights are played in order; clear them all to win the run.
    // ----------------------------------------------------------------------------
    inline const std::vector<std::vector<int>>& Encounters() {
        static const std::vector<std::vector<int>> e = {
            { 2 },// 1: lone slime  (tutorial-easy)
            { 0, 2 },// 2: jaw worm + slime
            { 1, 3 },// 3: cultist + spike slime
            { 0, 1, 2 },// 4: the gauntlet (mini-boss)
        };
        return e;
    }

}// namespace CardGame

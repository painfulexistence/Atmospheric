# CardBattle

A small **Slay-the-Spire / Darkest-Dungeon–style turn-based card battler** built
on the Atmospheric engine. Fight a sequence of encounters, draft a new card after
each win, and try to clear the run.

It is intentionally **asset-free** — everything is drawn with the engine's
immediate-mode 2D API (`DrawQuad` / `DrawRect` / `DrawCircle` / `DrawText`) using
the bundled default font, so there is nothing to import or convert.

## How to play

- **1–9** — play the card in that hand slot
- **Click a card** — play it (attacks hit the currently selected enemy)
- **Click an enemy** / **T** — choose the attack target
- **SPACE / E / click END TURN** — end your turn
- After a win, **1/2/3** (or click) picks a reward card, **SPACE** skips
- On the win / lose screen, **R** starts a new run

Each turn you regain energy, draw a fresh hand, and your Block resets. Enemies
telegraph their next move (e.g. `ATK 10`) above their heads so you can plan
whether to attack or defend.

## Design

### 1. Numbers are easy to adjust

**All content and tuning lives in [`game_data.hpp`](game_data.hpp)** and is pure
data — no logic. A balancer can freely edit it without touching gameplay code:

- `Tuning::*` — player HP, energy, cards drawn per turn, status multipliers,
  post-win heal, enemy pacing, …
- `CardDB()` — every card, expressed as a list of `{EffectKind, value}` effects.
  Adding a brand-new card is just one more data row.
- `EnemyDB()` — enemies and their move sets (also `{EffectKind, value}` lists).
- `Encounters()` — which enemies appear in each fight, and in what order.

Because cards and enemy moves share the same `Effect` vocabulary, the battle
system understands new content automatically.

### 2. Component-based architecture

Every combatant is a `GameObject` assembled from focused engine `Component`s,
and the whole fight is driven by another component:

| Component | Responsibility |
|---|---|
| `HealthComponent` | HP, max HP, per-turn Block, damage absorption |
| `StatusComponent` | Strength / Vulnerable / Weak / Poison stacks + damage math |
| `EnergyComponent` | the player's per-turn energy pool |
| `DeckComponent` | draw / hand / discard / exhaust piles, shuffling |
| `EnemyBrainComponent` | data-driven enemy AI + telegraphed intent |
| `CombatantComponent` | identity, on-screen rect, cached sibling components |
| `BattleManagerComponent` | the turn state machine + effect resolution |

`main.cpp` is a thin presentation layer: it lays out the screen, renders, and
turns mouse/keyboard into *intents* (`TryPlayCard`, `EndTurn`, `TakeReward`) on
the `BattleManagerComponent`. **No game rules live in the renderer.**

```
main.cpp ──intents──▶ BattleManagerComponent ──▶ Health/Status/Deck/Energy/Brain
   ▲                          (rules)                    (state)
   └────────────── queries (getters) for rendering ──────────────┘
```

## Build

The example is wired into the normal build; it produces a `CardBattle`
executable alongside the other examples.
